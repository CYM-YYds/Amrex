#include "AmrCoreLBM.H"

#include <AMReX_BoxList.H>
#include <AMReX_MultiFabUtil.H>
#include <AMReX_Utility.H>
#include <algorithm>
#include <optional>
#include "Kernels.H"

using namespace amrex;
using namespace Box3dDetail;

// OSI 地址转换、通信与粗细层传输。

void AmrCoreLBM::AverageDownOsiValidLevel(int lev, bool is_scale) {
    AMREX_ALWAYS_ASSERT(lev >= 0 && lev < finest_level);
    auto& coarse_state = osi_state.at(lev);
    const auto& fine_state = osi_state.at(lev + 1);
    const IntVect ratio = refRatio(lev);
    const auto fine_phase = osi_phase.at(lev + 1);
    const auto coarse_phase = osi_phase.at(lev);
    const Real scale = Real(2.0) * tau.at(lev) / tau.at(lev + 1);
    const BoxArray restricted_ba = amrex::coarsen(fine_state.boxArray(), ratio);

    // 这里使用函数局部 canonical 工作区，不保留跨调用同步缓冲。
    // 该路径只服务于完整 valid restriction，避免污染后续通信阶段的状态。
    MultiFab fine_canonical(
        fine_state.boxArray(), fine_state.DistributionMap(), Q, 0);
    MultiFab coarse_canonical(
        coarse_state.boxArray(), coarse_state.DistributionMap(), Q, 0);

    // 完整工作区一次解码全部 Q 分量，供 canonical restriction 使用。
    DecodeOsiValid(fine_state, fine_phase, fine_canonical);
    DecodeOsiValid(coarse_state, coarse_phase, coarse_canonical);

    if (is_scale) {
        for (MFIter mfi(fine_canonical, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
            const Box bx = mfi.tilebox();
            const auto fine = fine_canonical.array(mfi);
            amrex::ParallelFor(
                bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                    average_scale(i, j, k, fine, scale);
                });
        }
    }
    amrex::average_down(
        fine_canonical, coarse_canonical, 0, Q, ratio);

    for (MFIter mfi(coarse_state, false); mfi.isValid(); ++mfi) {
        const Box bx = mfi.validbox();
        const Box ring = amrex::grow(bx, coarse_state.nGrowVect());
        const auto [coarse_fab, coarse_shift] = OSI::MakeOsiFabContext(ring, coarse_phase);
        const auto src = coarse_canonical.const_array(mfi);
        const auto dst = coarse_state.array(mfi);
        // 只把细层有效盒粗化后的交集写回，保留 uncovered 粗单元的原值。
        for (const auto& intersection : restricted_ba.intersections(bx)) {
            const Box write_box = intersection.second;
            amrex::ParallelFor(
                write_box, Q,
                [=] AMREX_GPU_DEVICE(int i, int j, int k, int q) {
                    const auto raw = OSI::osi_address(
                        {i, j, k}, {e[q][0], e[q][1], lattice_ez(q)},
                        coarse_fab, coarse_shift);
                    dst(raw.x, raw.y, raw.z, q) = src(i, j, k, q);
                });
        }
    }
    amrex::Gpu::streamSynchronize();
}

void AmrCoreLBM::FillOsiGhostFromCoarse(
    int lev, amrex::Real time, bool apply_scale) {
    (void)time;
    const int fine_lev = lev + 1;
    AMREX_ALWAYS_ASSERT(lev >= 0 && fine_lev <= finest_level);

    auto& fine_state = osi_state.at(fine_lev);
    auto& coarse_stage = interpolation_cache.at(fine_lev).coarse_stage;
    const auto& patches = interpolation_cache.at(fine_lev).patches;
    const auto coarse_phase = osi_phase.at(lev);
    const auto fine_phase = osi_phase.at(fine_lev);
    const Real scale = tau.at(fine_lev) / tau.at(lev) / Real(2.0);

    if (patches.empty()) {
        return;
    }

    AMREX_ALWAYS_ASSERT(interpolation_cache.at(fine_lev).copy.ready);
    // 无本地模板的 rank 可以没有设备任务，但仍须参与跨 rank 搬运。
    const auto& cache = interpolation_cache.at(fine_lev).work;
    AMREX_ALWAYS_ASSERT(cache.tags.is_defined() && cache.scale_tags.is_defined());
    AMREX_ALWAYS_ASSERT(cache.tags.ntags == coarse_stage.local_size() &&
                        cache.scale_tags.ntags == coarse_stage.local_size());

    ParallelCopyOsi(interpolation_cache.at(fine_lev).copy,
                    osi_fab_phase_cache.at(lev).data(coarse_phase));

    ApplyPhysicalBoundaryToInterpolationStage(
        lev, coarse_stage);
    // 同一 stream 上先完成所有源缩放，再提交所有插值任务。
    if (apply_scale) {
        amrex::ParallelFor(cache.scale_tags,
                           [=] AMREX_GPU_DEVICE(int i, int j, int k, const amrex::Array4BoxTag<Real>& tag) {
                               average_scale(i, j, k, tag.dfab, scale);
                           });
    }
    const auto stages = coarse_stage.const_arrays();
    const auto fines = fine_state.arrays();
    const auto* shifts = osi_fab_phase_cache.at(fine_lev).data(fine_phase);
    if (interp_mode == 0) {
        amrex::ParallelFor(cache.tags,
                           [=] AMREX_GPU_DEVICE(int i, int j, int k, const InterpolationBatchTag& tag) {
                               interp_bilinear_ddf(i, j, k, fines[tag.fine_local_index], stages[tag.stage_local_index],
                                                   tag.fab, shifts[tag.phase_index]);
                           });
    } else if (interp_mode == 1) {
        amrex::ParallelFor(cache.tags,
                           [=] AMREX_GPU_DEVICE(int i, int j, int k, const InterpolationBatchTag& tag) {
                               interp_cell_cons_linear_children_ddf(i, j, k, fines[tag.fine_local_index], stages[tag.stage_local_index],
                                                                    tag.target, tag.fab, shifts[tag.phase_index]);
                           });
    } else {
        amrex::ParallelFor(cache.tags,
                           [=] AMREX_GPU_DEVICE(int i, int j, int k, const InterpolationBatchTag& tag) {
                               interp_cell_quadratic_children_ddf(i, j, k, fines[tag.fine_local_index], stages[tag.stage_local_index],
                                                                  tag.target, tag.fab, shifts[tag.phase_index]);
                           });
    }
}

void AmrCoreLBM::AverageDownOsiLevel(int fine_lev, bool is_scale) {
    const int coarse_lev = fine_lev - 1;
    AMREX_ALWAYS_ASSERT(fine_lev > 0 && fine_lev <= finest_level);
    auto& coarse_state = osi_state.at(coarse_lev);
    const auto& fine_state = osi_state.at(fine_lev);
    auto& interface_result = average_down_cache.at(coarse_lev).interface_buffer;
    const auto& fine_indices = average_down_cache.at(coarse_lev).fine_box_indices;
    const auto ratio = refRatio(coarse_lev);
    const auto coarse_phase = osi_phase.at(coarse_lev);
    const auto fine_phase = osi_phase.at(fine_lev);
    const Real scale = Real(2.0) * tau.at(coarse_lev) / tau.at(fine_lev);

    if (fine_indices.empty()) {
        return;
    }
    AMREX_ALWAYS_ASSERT(interface_result.size() == fine_indices.size());

#ifdef AMREX_USE_CUDA
    const auto& work = average_down_cache.at(coarse_lev).block_tags;
    if (!work.empty()) {
        const auto* tags = work.data();
        const auto outputs = interface_result.arrays();
        const auto fines = fine_state.const_arrays();
        const auto* shifts = osi_fab_phase_cache.at(fine_lev).data(fine_phase);
        amrex::launch<osi_restriction_threads_per_block>(
            static_cast<int>(work.size()), Gpu::Device::gpuStream(),
            [=] AMREX_GPU_DEVICE() noexcept {
                const auto tag = tags[blockIdx.x];
                const Long parent = tag.first_parent + threadIdx.x / osi_restriction_group_size;
                if (parent >= tag.region.numPts()) {
                    return;
                }
                const auto lo = amrex::lbound(tag.region);
                const int nx = tag.region.length(0), ny = tag.region.length(1);
                const int i = lo.x + static_cast<int>(parent % nx);
                const Long yz = parent / nx;
                const int j = lo.y + static_cast<int>(yz % ny);
                const int k = lo.z + static_cast<int>(yz / ny);
            // Q19 相邻线程处理相邻父单元；Q27 每半 warp 合作处理一个父单元。
#if Q == 27
                average_down_osi_to_canonical_half_warp(
                    i, j, k, threadIdx.x % osi_restriction_group_size,
                    outputs[tag.output_local_index], fines[tag.fine_local_index],
                    tag.fab, shifts[tag.phase_index], scale, is_scale);
#else
                average_down_osi_to_canonical(
                    i, j, k, outputs[tag.output_local_index], fines[tag.fine_local_index],
                    ratio, tag.fab, shifts[tag.phase_index], scale, is_scale);
#endif
            });
    }
#else
    // 每个稀疏 coarse interface Box 都映射到唯一的 fine Fab；直接按细层
    // phase 读取细子单元，不再解码完整细层。
    for (MFIter mfi(interface_result, false); mfi.isValid(); ++mfi) {
        const int fine_index = fine_indices.at(mfi.index());
        const Box fine_ring = amrex::grow(
            fine_state.boxArray()[fine_index], fine_state.nGrowVect());
        const auto [fine_fab, fine_shift] = OSI::MakeOsiFabContext(fine_ring, fine_phase);
        const Box bx = mfi.validbox();
        const auto coarse = interface_result.array(mfi);
        const auto fine = fine_state.const_array(fine_index);
        amrex::ParallelFor(
            bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                average_down_osi_to_canonical(
                    i, j, k, coarse, fine, ratio, fine_fab, fine_shift,
                    scale, is_scale);
            });
    }

#endif

    // 复用布局缓存，入口统一负责本地/MPI 分派和当前 phase 的地址映射。
    ParallelCopyOsi(
        average_down_cache.at(coarse_lev).copy,
        osi_fab_phase_cache.at(coarse_lev).data(coarse_phase),
        {&perf_stats.osi_mpi_pack, &perf_stats.osi_mpi_wait,
         &perf_stats.osi_mpi_unpack});
}

void AmrCoreLBM::BuildOsiCommunicationRegionCache(int lev) {
    auto& cache = osi_fillboundary_cache.at(lev);
    cache.clear();
    MultiFab& state = osi_state.at(lev);
    const auto& phases = osi_fab_phase_cache.at(lev);
    AMREX_ALWAYS_ASSERT(phases.matches(state));
    const BoxArray& ba = state.boxArray();
    const IntVect ng = state.nGrowVect();
    const std::uint64_t phase = osi_phase.at(lev);
    const auto fab_geometry = [&](int index) {
        return OSI::MakeOsiFabContext(amrex::grow(ba[index], ng), phase).fab;
    };
    // AMReX 的 FillBoundary 计划直接生成最终 GPU tag，省去仅供构建使用的配对列表。
    const auto& fb = state.getFB(ng, Geom(lev).periodicity());
    AMREX_ALWAYS_ASSERT(fb.m_LocTags && fb.m_SndTags && fb.m_RcvTags);
    Vector<OSI::LocalCopyTag> local_tags;
    local_tags.reserve(fb.m_LocTags->size());
    for (const auto& tag : *fb.m_LocTags) {
        local_tags.push_back({state.const_array(tag.srcIndex),
                              state.array(tag.dstIndex), tag.sbox, tag.dbox,
                              fab_geometry(tag.srcIndex),
                              fab_geometry(tag.dstIndex), phases.slot(tag.srcIndex),
                              phases.slot(tag.dstIndex)});
    }
    cache.local_tags.define(local_tags);
    cache.send_fabs.assign(ba.size(), 0);
    for (const auto& [peer, tags] : *fb.m_SndTags) {
        for (const auto& tag : tags) {
            cache.send_fabs[tag.srcIndex] = 1;
        }
    }
    int early = 0, deferred = 0;
    long long early_cells = 0, deferred_cells = 0;
    for (MFIter mfi(state, false); mfi.isValid(); ++mfi) {
        const auto cells = (amrex::grow(mfi.validbox(), nghost) &
                            Geom(lev).growPeriodicDomain(nghost))
                               .numPts();
        if (cache.send_fabs[mfi.index()]) {
            ++early;
            early_cells += cells;
        } else {
            ++deferred;
            deferred_cells += cells;
        }
    }
    cache.overlap_collision = early > 0 && deferred > 0;
    amrex::Print(amrex::Print::AllProcs)
        << "[OSI overlap layout] rank=" << ParallelDescriptor::MyProc()
        << " level=" << lev << " early_fabs=" << early
        << " deferred_fabs=" << deferred << " early_cells=" << early_cells
        << " deferred_cells=" << deferred_cells << '\n';

    const int nprocs = ParallelDescriptor::NProcs();
    const int my_rank = ParallelDescriptor::MyProc();
    cache.plan = OSI::make_mpi_plan(*fb.m_SndTags, *fb.m_RcvTags, nprocs, Q);
    std::size_t remote_records = 0;
    for (const auto& [peer, tags] : *fb.m_SndTags) {
        remote_records += tags.size();
    }
    for (const auto& [peer, tags] : *fb.m_RcvTags) {
        remote_records += tags.size();
    }
    OSI::define_mpi_tags(
        *fb.m_SndTags, *fb.m_RcvTags, cache.plan, Q,
        cache.pack_tags, cache.unpack_tags,
        [&](const auto& tag, std::size_t offset) -> OSI::MpiPackTag {
            return {state.const_array(tag.srcIndex), tag.sbox,
                    fab_geometry(tag.srcIndex), offset, phases.slot(tag.srcIndex)};
        },
        [&](const auto& tag, std::size_t offset) -> OSI::MpiUnpackTag {
            return {state.array(tag.dstIndex), tag.dbox,
                    fab_geometry(tag.dstIndex), offset, phases.slot(tag.dstIndex)};
        });
    cache.buffers.resize(cache.plan, osi_mpi_device_direct);

    int send_peers = 0;
    int recv_peers = 0;
    for (int peer = 0; peer < nprocs; ++peer) {
        send_peers += cache.plan.send_counts[peer] != 0;
        recv_peers += cache.plan.recv_counts[peer] != 0;
    }
    amrex::Print(amrex::Print::AllProcs)
        << "[OSI mpi layout] rank=" << my_rank << " level=" << lev
        << " send_peers=" << send_peers << " recv_peers=" << recv_peers
        << " send_bytes=" << cache.plan.send_total * sizeof(Real)
        << " recv_bytes=" << cache.plan.recv_total * sizeof(Real) << '\n';
    amrex::Print() << "[OSI comm cache] level=" << lev
                   << " amrex_plan=1 remote_records=" << remote_records << '\n';
}

void AmrCoreLBM::FillBoundaryOsi(int lev, const std::function<void()>& during_transfer) {
    ScopedPerfTimer timer(perf_stats.osi_fillboundary, params_.perf_osi_detail_timing);
    const std::uint64_t phase = osi_phase.at(lev);
    auto& cache = osi_fillboundary_cache.at(lev);
    const auto* phase_shifts = osi_fab_phase_cache.at(lev).data(phase);

    // 多 rank 时在投递远端 MPI 后启动本地 seam copy，使 GPU 工作与 MPI wait 重叠。
    const auto launch_local_copy = [&]() {
        amrex::ParallelFor(
            cache.local_tags,
            [=] AMREX_GPU_DEVICE(int i, int j, int k,
                                 const OSI::LocalCopyTag& tag) noexcept {
                const auto source = OSI::source_cell(tag, i, j, k);
                const int si = source[0];
                const int sj = source[1];
                const int sk = AMREX_D_PICK(0, 0, source[2]);
                const auto src_shift = phase_shifts[tag.src_phase_index];
                const auto dst_shift = phase_shifts[tag.dst_phase_index];
                for (int q = 0; q < Q; ++q) {
                    const auto src_raw = OSI::osi_address(
                        {si, sj, sk}, {e[q][0], e[q][1], lattice_ez(q)},
                        tag.src_fab, src_shift);
                    const auto dst_raw = OSI::osi_address(
                        {i, j, k}, {e[q][0], e[q][1], lattice_ez(q)},
                        tag.dst_fab, dst_shift);
                    tag.dst(dst_raw.x, dst_raw.y, dst_raw.z, q) =
                        tag.src(src_raw.x, src_raw.y, src_raw.z, q);
                }
            });
    };

    if (ParallelDescriptor::NProcs() == 1) {
        launch_local_copy();
        amrex::Gpu::streamSynchronize();
        return;
    }

#ifdef BL_USE_MPI
    const int nprocs = ParallelDescriptor::NProcs();
    const auto& send_offsets = cache.plan.send_offsets;
    const auto& recv_offsets = cache.plan.recv_offsets;
    const auto& send_counts = cache.plan.send_counts;
    const auto& recv_counts = cache.plan.recv_counts;
    auto& send_device = cache.buffers.send_device;
    auto& recv_device = cache.buffers.recv_device;
    auto& send_host = cache.buffers.send_host;
    auto& recv_host = cache.buffers.recv_host;
    // 通信缓冲按 tag 内 [q][cell] 排列，收发两端保持相同次序与字节总量。
    const bool pipeline_staging = osi_mpi_pipeline_chunk_bytes > 0;
    const std::size_t pipeline_chunk_reals = pipeline_staging
                                                 ? std::max<std::size_t>(
                                                       1, static_cast<std::size_t>(osi_mpi_pipeline_chunk_bytes) /
                                                              sizeof(Real))
                                                 : 0;
    bool local_copy_launched = false;

    {
        ScopedPerfTimer pack_timer(perf_stats.osi_mpi_pack, params_.perf_osi_detail_timing);
        {
            ScopedPerfTimer kernel_timer(perf_stats.osi_mpi_pack_kernel, params_.perf_osi_detail_timing);
            Real* buffer = send_device.data();
            amrex::ParallelFor(
                cache.pack_tags, Q,
                [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                                     const OSI::MpiPackTag& tag) noexcept {
                    const std::size_t cell = OSI::buffer_cell_index(tag.region, i, j, k);
                    const auto phase_shift = phase_shifts[tag.phase_index];
                    const auto raw = OSI::osi_address(
                        {i, j, k}, {e[q][0], e[q][1], lattice_ez(q)}, tag.fab,
                        phase_shift);
                    buffer[tag.offset + static_cast<std::size_t>(q) * tag.region.numPts() + cell] =
                        tag.src(raw.x, raw.y, raw.z, q);
                });
        }
        // D2H staging 在 stream 1 上提交；先明确等待 pack 所在的当前
        // stream，避免 stream 1 在 pack 尚未完成时读取 send_device。
        Gpu::streamSynchronize();
        // pack 已完成，剩余碰撞仅在 stream 0 排队；CPU 继续推进 staging/MPI。
        if (during_transfer) {
            during_transfer();
        }
        if (!osi_mpi_device_direct && !pipeline_staging &&
            !send_device.empty()) {
            if (osi_mpi_async_staging) {
                const double dtoh_start = amrex::second();
                // stream 1 只承载 staging copy；stream 0 继续执行本地 seam copy。
                Gpu::Device::setStreamIndex(1);
                Gpu::dtoh_memcpy_async(send_host.data(), send_device.data(),
                                       send_device.size() * sizeof(Real));
                Gpu::Device::resetStreamIndex();
                if (!during_transfer) {
                    launch_local_copy();
                    local_copy_launched = true;
                }
                Gpu::Device::setStreamIndex(1);
                Gpu::streamSynchronize();
                Gpu::Device::resetStreamIndex();
                if (params_.perf_osi_detail_timing) {
                    perf_stats.osi_mpi_dtoh += amrex::second() - dtoh_start;
                }
            } else {
                ScopedPerfTimer dtoh_timer(perf_stats.osi_mpi_dtoh, params_.perf_osi_detail_timing);
                Gpu::dtoh_memcpy(send_host.data(), send_device.data(),
                                 send_device.size() * sizeof(Real));
            }
        }
    }

    {
        ScopedPerfTimer wait_timer(perf_stats.osi_mpi_wait, params_.perf_osi_detail_timing);
        const int mpi_tag = ParallelDescriptor::SeqNum();
        Vector<ParallelDescriptor::Message> receives;
        Vector<ParallelDescriptor::Message> sends;
        Vector<std::size_t> recv_chunk_offsets;
        Vector<std::size_t> recv_chunk_counts;
        receives.reserve(nprocs);
        sends.reserve(nprocs);
        if (pipeline_staging) {
            for (int peer = 0; peer < nprocs; ++peer) {
                for (std::size_t done = 0; done < recv_counts[peer];
                     done += pipeline_chunk_reals) {
                    const std::size_t count = std::min(
                        pipeline_chunk_reals, recv_counts[peer] - done);
                    const std::size_t offset = recv_offsets[peer] + done;
                    receives.push_back(ParallelDescriptor::Arecv(
                        recv_host.data() + offset, count, peer, mpi_tag));
                    recv_chunk_offsets.push_back(offset);
                    recv_chunk_counts.push_back(count);
                }
            }
        } else {
            Real* recv_buffer = osi_mpi_device_direct
                                    ? recv_device.data()
                                    : recv_host.data();
            OSI::post_receives(receives, recv_counts, recv_offsets,
                               recv_buffer, nprocs, mpi_tag);
        }
        if (pipeline_staging && !local_copy_launched && !during_transfer) {
            launch_local_copy();
            local_copy_launched = true;
        }
        if (pipeline_staging) {
            for (int peer = 0; peer < nprocs; ++peer) {
                for (std::size_t done = 0; done < send_counts[peer];
                     done += pipeline_chunk_reals) {
                    const std::size_t count = std::min(
                        pipeline_chunk_reals, send_counts[peer] - done);
                    const std::size_t offset = send_offsets[peer] + done;
                    const double dtoh_start = amrex::second();
                    // 当前 chunk 搬运时，前一 chunk 已可在 MPI 中传输。
                    Gpu::Device::setStreamIndex(1);
                    Gpu::dtoh_memcpy_async(send_host.data() + offset,
                                           send_device.data() + offset,
                                           count * sizeof(Real));
                    Gpu::streamSynchronize();
                    Gpu::Device::resetStreamIndex();
                    if (params_.perf_osi_detail_timing) {
                        perf_stats.osi_mpi_dtoh += amrex::second() - dtoh_start;
                    }
                    sends.push_back(ParallelDescriptor::Asend(
                        send_host.data() + offset, count, peer, mpi_tag));
                }
            }
        } else {
            const Real* send_buffer = osi_mpi_device_direct
                                          ? send_device.data()
                                          : send_host.data();
            OSI::post_sends(sends, send_counts, send_offsets,
                            send_buffer, nprocs, mpi_tag);
        }
        // 此时发送源已打包且 MPI 已启动；剩余 Fab 与发送源存储互不重叠。
        // 本地复制必须等剩余碰撞结束，避免提前覆盖其 ghost 输入。
        if (!local_copy_launched) {
            launch_local_copy();
        }
        double pipeline_htod_start = 0.0;
        for (std::size_t i = 0; i < receives.size(); ++i) {
            receives[i].wait();
            if (pipeline_staging) {
                if (pipeline_htod_start == 0.0) {
                    pipeline_htod_start = amrex::second();
                }
                // 已到达 chunk 回传时，CPU 继续等待后续 receive。
                Gpu::Device::setStreamIndex(1);
                Gpu::htod_memcpy_async(
                    recv_device.data() + recv_chunk_offsets[i],
                    recv_host.data() + recv_chunk_offsets[i],
                    recv_chunk_counts[i] * sizeof(Real));
                Gpu::Device::resetStreamIndex();
            }
        }
        double htod_start = 0.0;
        if (osi_mpi_async_staging && !recv_device.empty()) {
            htod_start = amrex::second();
            // recv 完成后立即回传，CPU 同时等待非阻塞 send 收尾。
            Gpu::Device::setStreamIndex(1);
            Gpu::htod_memcpy_async(recv_device.data(), recv_host.data(),
                                   recv_device.size() * sizeof(Real));
            Gpu::Device::resetStreamIndex();
        }
        for (auto& message : sends) {
            message.wait();
        }
        if (pipeline_staging && pipeline_htod_start != 0.0) {
            Gpu::Device::setStreamIndex(1);
            Gpu::streamSynchronize();
            Gpu::Device::resetStreamIndex();
            if (params_.perf_osi_detail_timing) {
                perf_stats.osi_mpi_htod += amrex::second() - pipeline_htod_start;
            }
        }
        if (osi_mpi_async_staging && !recv_device.empty()) {
            Gpu::Device::setStreamIndex(1);
            Gpu::streamSynchronize();
            Gpu::Device::resetStreamIndex();
            if (params_.perf_osi_detail_timing) {
                perf_stats.osi_mpi_htod += amrex::second() - htod_start;
            }
        }
    }

    {
        ScopedPerfTimer unpack_timer(perf_stats.osi_mpi_unpack, params_.perf_osi_detail_timing);
        if (!osi_mpi_device_direct && !osi_mpi_async_staging &&
            !pipeline_staging &&
            !recv_device.empty()) {
            ScopedPerfTimer htod_timer(perf_stats.osi_mpi_htod, params_.perf_osi_detail_timing);
            Gpu::htod_memcpy(recv_device.data(), recv_host.data(),
                             recv_device.size() * sizeof(Real));
        }
        {
            ScopedPerfTimer kernel_timer(perf_stats.osi_mpi_unpack_kernel, params_.perf_osi_detail_timing);
            const Real* buffer = recv_device.data();
            amrex::ParallelFor(
                cache.unpack_tags, Q,
                [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                                     const OSI::MpiUnpackTag& tag) noexcept {
                    const std::size_t cell = OSI::buffer_cell_index(tag.region, i, j, k);
                    const auto phase_shift = phase_shifts[tag.phase_index];
                    const auto raw = OSI::osi_address(
                        {i, j, k}, {e[q][0], e[q][1], lattice_ez(q)}, tag.fab,
                        phase_shift);
                    tag.dst(raw.x, raw.y, raw.z, q) =
                        buffer[tag.offset + static_cast<std::size_t>(q) * tag.region.numPts() + cell];
                });
        }
    }
#else
    amrex::Abort("OSI remote communication requires an MPI build");
#endif
}

// 缓存搬运只更新当前相位，复用已有任务与缓冲；不重建 CPC 几何。
template <bool SourceRaw>
void AmrCoreLBM::ParallelCopyOsi(
    OSI::CpcCache<SourceRaw>& cache, const OSI::PhaseShift3* phase_shifts,
    OSI::CopyTiming timing) {
    AMREX_ALWAYS_ASSERT(cache.ready);
    // 关闭细分计时只移除计时指针；搬运与缓冲生命周期同步仍由执行层保证。
    if (!params_.perf_osi_detail_timing) {
        timing = {};
    }
    ScopedPerfTimer copy_timer(perf_stats.osi_parallel_copy, params_.perf_osi_detail_timing);
    OSI::parallel_copy_local(
        cache.local_tags, Q,
        [phase_shifts] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                                        const OSI::LocalCopyTag& tag) noexcept {
            const auto source = OSI::source_cell(tag, i, j, k);
            if constexpr (SourceRaw) {
                const auto raw = OSI::osi_address(
                    {source[0], source[1], AMREX_D_PICK(0, 0, source[2])},
                    {e[q][0], e[q][1], lattice_ez(q)}, tag.src_fab,
                    phase_shifts[tag.src_phase_index]);
                tag.dst(i, j, k, q) = tag.src(raw.x, raw.y, raw.z, q);
            } else {
                const auto raw = OSI::osi_address(
                    {i, j, k}, {e[q][0], e[q][1], lattice_ez(q)}, tag.dst_fab,
                    phase_shifts[tag.dst_phase_index]);
                tag.dst(raw.x, raw.y, raw.z, q) =
                    tag.src(source[0], source[1], AMREX_D_PICK(0, 0, source[2]), q);
            }
        });
    const int nprocs = ParallelDescriptor::NProcs();
    if (nprocs == 1) {
        // 平均写回保留同步；插值随后在同一 stream 消费暂存数据。
        if constexpr (!SourceRaw) {
            Gpu::streamSynchronize();
        }
        return;
    }
    using PackTag = typename OSI::CpcCache<SourceRaw>::PackTag;
    using UnpackTag = typename OSI::CpcCache<SourceRaw>::UnpackTag;
    OSI::parallel_copy_mpi(
        cache.pack_tags, cache.unpack_tags, cache.plan,
        osi_mpi_device_direct, Q, nprocs,
        [phase_shifts] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                                        const PackTag& tag, Real* buffer) noexcept {
            const std::size_t cell = OSI::buffer_cell_index(tag.region, i, j, k);
            if constexpr (SourceRaw) {
                const auto raw = OSI::osi_address(
                    {i, j, k}, {e[q][0], e[q][1], lattice_ez(q)}, tag.fab,
                    phase_shifts[tag.phase_index]);
                buffer[tag.offset + static_cast<std::size_t>(q) * tag.region.numPts() + cell] = tag.src(raw.x, raw.y, raw.z, q);
            } else {
                buffer[tag.offset + static_cast<std::size_t>(q) * tag.region.numPts() + cell] = tag.src(i, j, k, q);
            }
        },
        [phase_shifts] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                                        const UnpackTag& tag, const Real* buffer) noexcept {
            const std::size_t cell = OSI::buffer_cell_index(tag.region, i, j, k);
            if constexpr (SourceRaw) {
                tag.dst(i, j, k, q) = buffer[tag.offset + static_cast<std::size_t>(q) * tag.region.numPts() + cell];
            } else {
                const auto raw = OSI::osi_address(
                    {i, j, k}, {e[q][0], e[q][1], lattice_ez(q)}, tag.fab,
                    phase_shifts[tag.phase_index]);
                tag.dst(raw.x, raw.y, raw.z, q) = buffer[tag.offset + static_cast<std::size_t>(q) * tag.region.numPts() + cell];
            }
        },
        timing, &cache.buffers);
}

void AmrCoreLBM::ParallelCopyOsi(
    int lev, const MultiFab& source, std::uint64_t source_phase,
    MultiFab& destination, std::uint64_t destination_phase) {
    const auto& cpc = destination.getCPC(
        IntVect(0), source, IntVect(0), Geom(lev).periodicity());
    AMREX_ALWAYS_ASSERT(cpc.m_LocTags && cpc.m_SndTags && cpc.m_RcvTags);

    // 同一 rank 的重叠区域直接执行 source raw -> destination raw，源和目标
    // 使用各自 Fab 的几何，并允许调用者显式指定两侧 phase。
    Vector<OSI::LocalCopyTag> local_tags;
    local_tags.reserve(cpc.m_LocTags->size());
    for (const auto& tag : *cpc.m_LocTags) {
        const Box source_ring = amrex::grow(
            source.boxArray()[tag.srcIndex], source.nGrowVect());
        const Box destination_ring = amrex::grow(
            destination.boxArray()[tag.dstIndex], destination.nGrowVect());
        const auto source_lo = source_ring.smallEnd();
        const auto destination_lo = destination_ring.smallEnd();
        local_tags.push_back({source.const_array(tag.srcIndex),
                              destination.array(tag.dstIndex),
                              tag.sbox,
                              tag.dbox,
                              {{source_lo[0], source_lo[1], AMREX_D_PICK(0, 0, source_lo[2])},
                               {source_ring.length(0), source_ring.length(1),
                                AMREX_D_PICK(1, 1, source_ring.length(2))}},
                              {{destination_lo[0], destination_lo[1], AMREX_D_PICK(0, 0, destination_lo[2])},
                               {destination_ring.length(0), destination_ring.length(1),
                                AMREX_D_PICK(1, 1, destination_ring.length(2))}}});
    }

    TagVector<OSI::LocalCopyTag> local_tv(local_tags);
    OSI::parallel_copy_local(
        local_tv, Q,
        [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                             const OSI::LocalCopyTag& tag) noexcept {
            const auto source = OSI::source_cell(tag, i, j, k);
            const auto source_shift =
                OSI::osi_phase_shift(source_phase, tag.src_fab);
            const auto source_raw = OSI::osi_address(
                {source[0], source[1], AMREX_D_PICK(0, 0, source[2])},
                {e[q][0], e[q][1], lattice_ez(q)}, tag.src_fab, source_shift);
            const auto destination_shift =
                OSI::osi_phase_shift(destination_phase, tag.dst_fab);
            const auto destination_raw = OSI::osi_address(
                {i, j, k}, {e[q][0], e[q][1], lattice_ez(q)},
                tag.dst_fab, destination_shift);
            tag.dst(destination_raw.x, destination_raw.y,
                    destination_raw.z, q) =
                tag.src(source_raw.x, source_raw.y, source_raw.z, q);
        });

    if (ParallelDescriptor::NProcs() == 1) {
        Gpu::streamSynchronize();
        return;
    }

    const int nprocs = ParallelDescriptor::NProcs();
    const auto plan = OSI::make_mpi_plan(
        *cpc.m_SndTags, *cpc.m_RcvTags, nprocs, Q);
    Vector<OSI::RawPackTag> pack_tags;
    Vector<OSI::MpiUnpackTag> unpack_tags;
    Vector<std::size_t> send_cursor = plan.send_offsets;
    Vector<std::size_t> recv_cursor = plan.recv_offsets;

    for (const auto& [peer, tags] : *cpc.m_SndTags) {
        for (const auto& tag : tags) {
            const Box source_ring = amrex::grow(
                source.boxArray()[tag.srcIndex], source.nGrowVect());
            const auto source_lo = source_ring.smallEnd();
            pack_tags.push_back({source.const_array(tag.srcIndex), tag.sbox, {{source_lo[0], source_lo[1], AMREX_D_PICK(0, 0, source_lo[2])}, {source_ring.length(0), source_ring.length(1), AMREX_D_PICK(1, 1, source_ring.length(2))}}, send_cursor[peer]});
            send_cursor[peer] +=
                static_cast<std::size_t>(tag.sbox.numPts()) * Q;
        }
    }

    for (const auto& [peer, tags] : *cpc.m_RcvTags) {
        for (const auto& tag : tags) {
            const Box destination_ring = amrex::grow(
                destination.boxArray()[tag.dstIndex], destination.nGrowVect());
            const auto destination_lo = destination_ring.smallEnd();
            unpack_tags.push_back({destination.array(tag.dstIndex), tag.dbox, {{destination_lo[0], destination_lo[1], AMREX_D_PICK(0, 0, destination_lo[2])}, {destination_ring.length(0), destination_ring.length(1), AMREX_D_PICK(1, 1, destination_ring.length(2))}}, recv_cursor[peer]});
            recv_cursor[peer] +=
                static_cast<std::size_t>(tag.dbox.numPts()) * Q;
        }
    }

    TagVector<OSI::RawPackTag> pack_tv(pack_tags);
    TagVector<OSI::MpiUnpackTag> unpack_tv(unpack_tags);
    OSI::parallel_copy_mpi(
        pack_tv, unpack_tv, plan, osi_mpi_device_direct, Q, nprocs,
        [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                             const OSI::RawPackTag& tag,
                             Real* send_buffer) noexcept {
            const auto lo = tag.region.smallEnd();
            const int nx = tag.region.length(0);
            const int ny = tag.region.length(1);
            const std::size_t cell = static_cast<std::size_t>(
                ((k - AMREX_D_PICK(0, 0, lo[2])) * ny + (j - lo[1])) * nx + (i - lo[0]));
            const auto shift = OSI::osi_phase_shift(source_phase, tag.fab);
            const auto raw = OSI::osi_address(
                {i, j, k}, {e[q][0], e[q][1], lattice_ez(q)}, tag.fab, shift);
            send_buffer[tag.offset + static_cast<std::size_t>(q) * tag.region.numPts() + cell] =
                tag.src(raw.x, raw.y, raw.z, q);
        },
        [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                             const OSI::MpiUnpackTag& tag,
                             const Real* receive_buffer) noexcept {
            const auto lo = tag.region.smallEnd();
            const int nx = tag.region.length(0);
            const int ny = tag.region.length(1);
            const std::size_t cell = static_cast<std::size_t>(
                ((k - AMREX_D_PICK(0, 0, lo[2])) * ny + (j - lo[1])) * nx + (i - lo[0]));
            const auto shift =
                OSI::osi_phase_shift(destination_phase, tag.fab);
            const auto raw = OSI::osi_address(
                {i, j, k}, {e[q][0], e[q][1], lattice_ez(q)}, tag.fab, shift);
            tag.dst(raw.x, raw.y, raw.z, q) =
                receive_buffer[tag.offset + static_cast<std::size_t>(q) * tag.region.numPts() + cell];
        });
}

void AmrCoreLBM::DecodeOsiValid(
    const MultiFab& state, std::uint64_t phase, MultiFab& canonical) const {
    AMREX_ALWAYS_ASSERT(state.boxArray() == canonical.boxArray());
    AMREX_ALWAYS_ASSERT(state.DistributionMap() == canonical.DistributionMap());
    AMREX_ALWAYS_ASSERT(canonical.nComp() == Q);

    for (MFIter mfi(state, false); mfi.isValid(); ++mfi) {
        const Box bx = mfi.validbox();
        const Box ring = amrex::grow(bx, state.nGrowVect());
        const auto [fab, phase_shift] = OSI::MakeOsiFabContext(ring, phase);
        const auto src = state.const_array(mfi);
        const auto dst = canonical.array(mfi);
        amrex::ParallelFor(
            bx, Q,
            [=] AMREX_GPU_DEVICE(int i, int j, int k, int q) noexcept {
                const auto raw = OSI::osi_address(
                    {i, j, k}, {e[q][0], e[q][1], lattice_ez(q)}, fab,
                    phase_shift);
                dst(i, j, k, q) = src(raw.x, raw.y, raw.z, q);
            });
    }
}
