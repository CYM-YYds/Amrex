#include "AmrCoreLBM.H"
#include "AmrCoreLBM_detail.H"

#include <AMReX_BoxList.H>
#include <AMReX_MultiFabUtil.H>
#include <AMReX_PhysBCFunct.H>
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
                        {i, j, k}, {e[q][0], e[q][1], e[q][2]},
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
    AMREX_ALWAYS_ASSERT(refRatio(lev) == IntVect(2));
    AMREX_ALWAYS_ASSERT(interp_direct_cache_ready.at(fine_lev));

    auto& coarse_state = osi_state.at(lev);
    auto& fine_state = osi_state.at(fine_lev);
    auto& coarse_stage = interp_direct_coarse_stage.at(fine_lev);
    const auto& fine_work_boxes = interp_direct_fine_boxes.at(fine_lev);
    const auto& fine_indices = interp_direct_fine_index.at(fine_lev);
    const auto coarse_phase = osi_phase.at(lev);
    const auto fine_phase = osi_phase.at(fine_lev);
    const Real scale = tau.at(fine_lev) / tau.at(lev) / Real(2.0);

    if (fine_indices.empty()) {
        return;
    }

    if (osi_parallel_copy && ParallelDescriptor::NProcs() > 1) {
        ScopedPerfTimer timer(perf_stats.osi_parallel_copy);
        // CPC 的目标是 sparse coarse_stage，源是 coarse OSI raw；本路径只
        // 复用 CPC 的空间/归属配对，不让 AMReX 直接解释 OSI raw 分量。
        const auto& cpc = coarse_stage.getCPC(
            IntVect(0), coarse_state, IntVect(0),
            Geom(lev).periodicity()); // 以 coarse_stage 为目标、coarse_state 为源，结合两侧的 ghost 偏移和 当前粗层周期性，生成本 rank 直接复制以及跨 rank 收发所需的 CPC 标签。
        AMREX_ALWAYS_ASSERT(cpc.m_LocTags && cpc.m_SndTags && cpc.m_RcvTags);

        Vector<OSI::LocalCopyTag> local_tags;
        local_tags.reserve(cpc.m_LocTags->size());
        for (const auto& tag : *cpc.m_LocTags) {
            const Box source_ring = amrex::grow(
                coarse_state.boxArray()[tag.srcIndex],
                coarse_state.nGrowVect());
            const auto source_fab =
                OSI::MakeOsiFabContext(source_ring, coarse_phase).fab;
            local_tags.push_back({coarse_state.const_array(tag.srcIndex),
                                  coarse_stage.array(tag.dstIndex), tag.sbox,
                                  tag.dbox, source_fab, OSI::FabGeometry{}});
        }
        TagVector<OSI::LocalCopyTag> local_tv(local_tags);

        const int nprocs = ParallelDescriptor::NProcs();
        const auto plan = OSI::make_mpi_plan(
            *cpc.m_SndTags, *cpc.m_RcvTags, nprocs, Q);
        const auto& send_counts = plan.send_counts;
        const auto& recv_counts = plan.recv_counts;
        const auto& send_offsets = plan.send_offsets;
        const auto& recv_offsets = plan.recv_offsets;
        const auto send_total = plan.send_total;
        const auto recv_total = plan.recv_total;

        Vector<OSI::RawPackTag> pack_tags;
        Vector<OSI::CanonicalUnpackTag> unpack_tags;
        Vector<std::size_t> send_cursor = send_offsets;
        Vector<std::size_t> recv_cursor = recv_offsets;
        for (const auto& [peer, tags] : *cpc.m_SndTags) {
            for (const auto& tag : tags) {
                const Box source_ring = amrex::grow(
                    coarse_state.boxArray()[tag.srcIndex],
                    coarse_state.nGrowVect());
                const auto source_fab =
                    OSI::MakeOsiFabContext(source_ring, coarse_phase).fab;
                pack_tags.push_back({coarse_state.const_array(tag.srcIndex),
                                     tag.sbox, source_fab,
                                     send_cursor[peer]});
                send_cursor[peer] +=
                    static_cast<std::size_t>(tag.sbox.numPts()) * Q;
            }
        }
        for (const auto& [peer, tags] : *cpc.m_RcvTags) {
            for (const auto& tag : tags) {
                unpack_tags.push_back({coarse_stage.array(tag.dstIndex),
                                       tag.dbox, recv_cursor[peer]});
                recv_cursor[peer] +=
                    static_cast<std::size_t>(tag.dbox.numPts()) * Q;
            }
        }

        TagVector<OSI::RawPackTag> pack_tv(pack_tags);
        TagVector<OSI::CanonicalUnpackTag> unpack_tv(unpack_tags);

        amrex::Print(amrex::Print::AllProcs)
            << "[OSI interpolation mpi] fine_level=" << fine_lev
            << " transport="
            << (osi_mpi_device_direct ? "device-direct" : "host-staging")
            << " send_bytes=" << send_total * sizeof(Real)
            << " recv_bytes=" << recv_total * sizeof(Real) << '\n';

        OSI::parallel_copy_local(
            local_tv, Q,
            [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                                 const OSI::LocalCopyTag& tag) noexcept {
                const auto source = OSI::source_cell(tag, i, j, k);
                const auto phase_shift =
                    OSI::osi_phase_shift(coarse_phase, tag.src_fab);
                const auto raw = OSI::osi_address(
                    {source[0], source[1], source[2]},
                    {e[q][0], e[q][1], e[q][2]}, tag.src_fab,
                    phase_shift);
                tag.dst(i, j, k, q) = tag.src(raw.x, raw.y, raw.z, q);
            });

        OSI::parallel_copy_mpi(
            pack_tv, unpack_tv, plan, osi_mpi_device_direct, Q, nprocs,
            [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                                 const OSI::RawPackTag& tag,
                                 Real* buffer) noexcept {
                const auto lo = tag.region.smallEnd();
                const int nx = tag.region.length(0);
                const int ny = tag.region.length(1);
                const std::size_t cell = static_cast<std::size_t>(
                    ((k - lo[2]) * ny + (j - lo[1])) * nx +
                    (i - lo[0]));
                const auto phase_shift =
                    OSI::osi_phase_shift(coarse_phase, tag.fab);
                const auto raw = OSI::osi_address(
                    {i, j, k}, {e[q][0], e[q][1], e[q][2]}, tag.fab,
                    phase_shift);
                buffer[tag.offset + cell * Q + q] =
                    tag.src(raw.x, raw.y, raw.z, q);
            },
            [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                                 const OSI::CanonicalUnpackTag& tag,
                                 const Real* buffer) noexcept {
                const auto lo = tag.region.smallEnd();
                const int nx = tag.region.length(0);
                const int ny = tag.region.length(1);
                const std::size_t cell = static_cast<std::size_t>(
                    ((k - lo[2]) * ny + (j - lo[1])) * nx + (i - lo[0]));
                tag.dst(i, j, k, q) =
                    buffer[tag.offset + cell * Q + q];
            });
    } else if (osi_parallel_copy && ParallelDescriptor::NProcs() == 1 &&
               osi_interp_local_copy_tags.at(fine_lev).ntags != 0) {
        ScopedPerfTimer timer(perf_stats.osi_parallel_copy);
        // direct 路径无需遵循通信缓冲的分批限制，一次处理全部 Q 分量，
        // 避免把本地 raw-to-canonical 复制拆成 9 次 kernel launch。
        OSI::parallel_copy_local(
            osi_interp_local_copy_tags.at(fine_lev), Q,
            [=] AMREX_GPU_DEVICE(int i, int j, int k, int n,
                                 const OSI::LocalCopyTag& tag) noexcept {
                const auto source = OSI::source_cell(tag, i, j, k);
                const int q = n;
                const auto phase_shift =
                    OSI::osi_phase_shift(coarse_phase, tag.src_fab);
                const auto raw = OSI::osi_address(
                    {source[0], source[1], source[2]},
                    {e[q][0], e[q][1], e[q][2]}, tag.src_fab,
                    phase_shift);
                tag.dst(i, j, k, n) = tag.src(raw.x, raw.y, raw.z, q);
            });
    } else {
        MultiFab decode_canonical(
            coarse_state.boxArray(), coarse_state.DistributionMap(),
            Q, coarse_state.nGrowVect());
        // direct 关闭时仍使用 canonical 回退，但一次处理完整 Q 分量。
        DecodeOsiValid(coarse_state, coarse_phase, decode_canonical);
        coarse_stage.ParallelCopy(
            decode_canonical, 0, 0, Q, IntVect(0), IntVect(0),
            Geom(lev).periodicity());
    }

    ApplyPhysicalBoundaryToInterpolationStage(lev);

    for (MFIter mfi(coarse_stage, false); mfi.isValid(); ++mfi) {
        const int stage_index = mfi.index();
        const Box stage_box = mfi.validbox();
        const auto coarse = coarse_stage.array(mfi);
        if (apply_scale) {
            amrex::ParallelFor(
                stage_box, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                    average_scale(i, j, k, coarse, scale);
                });
        }

        const int fine_index = fine_indices.at(stage_index);
        const Box fine_ring = amrex::grow(
            fine_state.boxArray()[fine_index], fine_state.nGrowVect());
        const auto [fine_fab, fine_shift] = OSI::MakeOsiFabContext(fine_ring, fine_phase);

        const auto fine = fine_state.array(fine_index);
        const auto coarse_const = coarse_stage.const_array(mfi);
        const Box fine_box = fine_work_boxes.at(stage_index);
        if (interp_mode == 0) {
            amrex::ParallelFor(
                fine_box, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                    interp_bilinear_d3q(
                        i, j, k, fine, coarse_const, fine_fab, fine_shift);
                });
        } else {
            const Box coarse_parent_box = amrex::coarsen(fine_box, 2);
            if (interp_mode == 1) {
                amrex::ParallelFor(
                    coarse_parent_box,
                    [=] AMREX_GPU_DEVICE(int ic, int jc, int kc) {
                        interp_cell_cons_linear_children_d3q(
                            ic, jc, kc, fine, coarse_const, fine_box,
                            fine_fab, fine_shift);
                    });
            } else {
                amrex::ParallelFor(
                    coarse_parent_box,
                    [=] AMREX_GPU_DEVICE(int ic, int jc, int kc) {
                        interp_cell_quadratic_children_d3q(
                            ic, jc, kc, fine, coarse_const, fine_box,
                            fine_fab, fine_shift);
                    });
            }
        }
    }
}

void AmrCoreLBM::AverageDownOsiLevel(int fine_lev, bool is_scale) {
    const int coarse_lev = fine_lev - 1;
    AMREX_ALWAYS_ASSERT(fine_lev > 0 && fine_lev <= finest_level);
    auto& coarse_state = osi_state.at(coarse_lev);
    const auto& fine_state = osi_state.at(fine_lev);
    auto& interface_result = average_interface_buffer.at(coarse_lev);
    const auto& fine_indices = average_interface_fine_box.at(coarse_lev);
    const auto ratio = refRatio(coarse_lev);
    const auto coarse_phase = osi_phase.at(coarse_lev);
    const auto fine_phase = osi_phase.at(fine_lev);
    const Real scale = Real(2.0) * tau.at(coarse_lev) / tau.at(fine_lev);

    if (fine_indices.empty()) {
        return;
    }
    AMREX_ALWAYS_ASSERT(interface_result.size() == fine_indices.size());

    // 每个稀疏 coarse interface Box 都映射到唯一的 fine Fab；直接按细层
    // phase 读取 2x2x2 子单元，不再解码完整细层。
    for (MFIter mfi(interface_result, false); mfi.isValid(); ++mfi) {
        const int fine_index = fine_indices.at(mfi.index());
        const Box fine_ring = amrex::grow(
            fine_state.boxArray()[fine_index], fine_state.nGrowVect());
        const auto [fine_fab, fine_shift] = OSI::MakeOsiFabContext(fine_ring, fine_phase);
        const Box bx = mfi.validbox();
        const auto coarse = interface_result.array(mfi);
        const auto fine = fine_state.const_array(fine_index);
#ifdef AMREX_USE_CUDA
        // 与 A-B 融合 restriction 保持相同的 warp 组织：一个
        // warp 负责一个 coarse parent，lane 0..26 分别处理一个
        // D3Q27 分量，避免单线程保留 fq[Q] 和 restricted[Q]。
        constexpr int threads_per_block = 256;
        constexpr int warp_size = 32;
        constexpr int warps_per_block = threads_per_block / warp_size;
        static_assert(Q <= warp_size,
                      "warp-per-parent restriction requires Q <= 32");
        const Long ncells = bx.numPts();
        const int nblocks = static_cast<int>(
            (ncells + warps_per_block - 1) / warps_per_block);
        const auto bx_lo = amrex::lbound(bx);
        const int nx = bx.length(0);
        const int ny = bx.length(1);
        amrex::launch<threads_per_block>(
            nblocks, amrex::Gpu::Device::gpuStream(),
            [=] AMREX_GPU_DEVICE() noexcept {
                const int lane = threadIdx.x % warp_size;
                const int warp_in_block = threadIdx.x / warp_size;
                const Long icell =
                    static_cast<Long>(blockIdx.x) * warps_per_block +
                    warp_in_block;
                if (icell < ncells) {
                    const int i =
                        bx_lo.x + static_cast<int>(icell % nx);
                    const Long yz = icell / nx;
                    const int j =
                        bx_lo.y + static_cast<int>(yz % ny);
                    const int k = bx_lo.z + static_cast<int>(yz / ny);
                    average_down_osi_to_canonical_warp(
                        i, j, k, lane, coarse, fine, ratio, fine_fab,
                        fine_shift, scale, is_scale);
                }
            });
#else
        amrex::ParallelFor(
            bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                average_down_osi_to_canonical(
                    i, j, k, coarse, fine, ratio, fine_fab, fine_shift,
                    scale, is_scale);
            });
#endif
    }

    // 多 rank direct 路径使用平均专用 CPC：canonical restriction 结果在源
    // rank 打包，目标 rank 直接按 coarse phase 解包到 OSI raw。
    if (osi_parallel_copy && ParallelDescriptor::NProcs() > 1) {
        ScopedPerfTimer direct_timer(perf_stats.osi_parallel_copy);
        const auto& cpc = coarse_state.getCPC(
            IntVect(0), interface_result, IntVect(0),
            Geom(coarse_lev).periodicity());
        AMREX_ALWAYS_ASSERT(cpc.m_LocTags && cpc.m_SndTags && cpc.m_RcvTags);

        Vector<OSI::LocalCopyTag> local_tags;
        local_tags.reserve(cpc.m_LocTags->size());
        for (const auto& tag : *cpc.m_LocTags) {
            const Box ring = amrex::grow(
                coarse_state.boxArray()[tag.dstIndex],
                coarse_state.nGrowVect());
            const auto dst_fab =
                OSI::MakeOsiFabContext(ring, coarse_phase).fab;
            local_tags.push_back({interface_result.const_array(tag.srcIndex),
                                  coarse_state.array(tag.dstIndex), tag.sbox, tag.dbox,
                                  OSI::FabGeometry{}, dst_fab});
        }
        TagVector<OSI::LocalCopyTag> local_tv(local_tags);

        const int nprocs = ParallelDescriptor::NProcs();
        const auto plan = OSI::make_mpi_plan(
            *cpc.m_SndTags, *cpc.m_RcvTags, nprocs, Q);
        const auto& send_counts = plan.send_counts;
        const auto& recv_counts = plan.recv_counts;
        const auto& send_offsets = plan.send_offsets;
        const auto& recv_offsets = plan.recv_offsets;
        const auto send_total = plan.send_total;
        const auto recv_total = plan.recv_total;

        Vector<OSI::CanonicalPackTag> pack_tags;
        Vector<OSI::CanonicalRawUnpackTag> unpack_tags;
        Vector<std::size_t> send_cursor = send_offsets;
        Vector<std::size_t> recv_cursor = recv_offsets;
        for (const auto& [peer, tags] : *cpc.m_SndTags) {
            for (const auto& tag : tags) {
                pack_tags.push_back({interface_result.const_array(tag.srcIndex), tag.sbox,
                                     send_cursor[peer]});
                send_cursor[peer] +=
                    static_cast<std::size_t>(tag.sbox.numPts()) * Q;
            }
        }
        for (const auto& [peer, tags] : *cpc.m_RcvTags) {
            for (const auto& tag : tags) {
                const Box ring = amrex::grow(
                    coarse_state.boxArray()[tag.dstIndex],
                    coarse_state.nGrowVect());
                const auto dst_fab =
                    OSI::MakeOsiFabContext(ring, coarse_phase).fab;
                unpack_tags.push_back({coarse_state.array(tag.dstIndex), tag.dbox, dst_fab,
                                       recv_cursor[peer]});
                recv_cursor[peer] +=
                    static_cast<std::size_t>(tag.dbox.numPts()) * Q;
            }
        }
        TagVector<OSI::CanonicalPackTag> pack_tv(pack_tags);
        TagVector<OSI::CanonicalRawUnpackTag> unpack_tv(unpack_tags);

        amrex::Print(amrex::Print::AllProcs)
            << "[OSI average mpi] fine_level=" << fine_lev
            << " transport="
            << (osi_mpi_device_direct ? "device-direct" : "host-staging")
            << " send_bytes=" << send_total * sizeof(Real)
            << " recv_bytes=" << recv_total * sizeof(Real) << '\n';

        OSI::parallel_copy_local(
            local_tv, Q,
            [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                                 const OSI::LocalCopyTag& tag) noexcept {
                const auto source = OSI::source_cell(tag, i, j, k);
                const auto shift =
                    OSI::osi_phase_shift(coarse_phase, tag.dst_fab);
                const auto raw = OSI::osi_address(
                    {i, j, k}, {e[q][0], e[q][1], e[q][2]},
                    tag.dst_fab, shift);
                tag.dst(raw.x, raw.y, raw.z, q) =
                    tag.src(source[0], source[1], source[2], q);
            });

        OSI::parallel_copy_mpi(
            pack_tv, unpack_tv, plan, osi_mpi_device_direct, Q, nprocs,
            [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                                 const OSI::CanonicalPackTag& tag,
                                 Real* buffer) noexcept {
                const std::size_t cell =
                    OSI::buffer_cell_index(tag.region, i, j, k);
                buffer[tag.offset + cell * Q + q] = tag.src(i, j, k, q);
            },
            [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                                 const OSI::CanonicalRawUnpackTag& tag,
                                 const Real* buffer) noexcept {
                const std::size_t cell =
                    OSI::buffer_cell_index(tag.region, i, j, k);
                const auto shift =
                    OSI::osi_phase_shift(coarse_phase, tag.fab);
                const auto raw = OSI::osi_address(
                    {i, j, k}, {e[q][0], e[q][1], e[q][2]},
                    tag.fab, shift);
                tag.dst(raw.x, raw.y, raw.z, q) =
                    buffer[tag.offset + cell * Q + q];
            },
            {&perf_stats.osi_mpi_pack, &perf_stats.osi_mpi_wait,
             &perf_stats.osi_mpi_unpack});
        return;
    }

    // 单 rank 直接将全部 Q 分量写回 coarse OSI raw state，无需分批暂存。
    if (osi_parallel_copy && ParallelDescriptor::NProcs() == 1) {
        ScopedPerfTimer copy_timer(perf_stats.osi_parallel_copy);
        for (MFIter src_mfi(interface_result, false); src_mfi.isValid();
             ++src_mfi) {
            const int src_index = src_mfi.index();
            Vector<std::pair<int, Box>> intersections;
            coarse_state.boxArray().intersections(
                interface_result.boxArray()[src_index], intersections,
                false, IntVect::TheZeroVector()); // 只匹配 coarse valid Box
            const auto src = interface_result.const_array(src_mfi);
            for (const auto& is : intersections) {
                const int dst_index = is.first;
                const Box bx = is.second & coarse_state.boxArray()[dst_index];
                const Box ring = amrex::grow(
                    coarse_state.boxArray()[dst_index],
                    coarse_state.nGrowVect());
                const auto [coarse_fab, coarse_shift] = OSI::MakeOsiFabContext(ring, coarse_phase);
                const auto dst = coarse_state.array(dst_index);
                amrex::ParallelFor(
                    bx, Q,
                    [=] AMREX_GPU_DEVICE(int i, int j, int k, int n) {
                        const int q = n;
                        const auto raw = OSI::osi_address(
                            {i, j, k}, {e[q][0], e[q][1], e[q][2]},
                            coarse_fab, coarse_shift);
                        dst(raw.x, raw.y, raw.z, q) = src(i, j, k, n);
                    });
            }
        }
        amrex::Gpu::streamSynchronize();
        return;
    }

    // 回退路径使用完整 Q 分量的 canonical 暂存，一次写回所有方向。
    MultiFab transfer_canonical(
        coarse_state.boxArray(), coarse_state.DistributionMap(),
        Q, IntVect(0));
    transfer_canonical.ParallelCopy(
        interface_result, 0, 0, Q, IntVect(0), IntVect(0));

    for (MFIter mfi(coarse_state, false); mfi.isValid(); ++mfi) {
        const Box ring =
            amrex::grow(mfi.validbox(), coarse_state.nGrowVect());
        const auto [coarse_fab, coarse_shift] = OSI::MakeOsiFabContext(ring, coarse_phase);
        const auto mask = interface_mask.at(coarse_lev).const_array(mfi);
        const auto src = transfer_canonical.const_array(mfi);
        const auto dst = coarse_state.array(mfi);
        Vector<std::pair<int, Box>> intersections;
        interface_result.boxArray().intersections(
            mfi.validbox(), intersections, false, IntVect(0));
        // staging 只有稀疏结果覆盖处被填充，写回也必须限制到这些交集。
        for (const auto& intersection : intersections) {
            const Box& bx = intersection.second;
            amrex::ParallelFor(
                bx, Q,
                [=] AMREX_GPU_DEVICE(int i, int j, int k, int q) {
                    if (mask(i, j, k) == 0) {
                        return;
                    }
                    const auto raw = OSI::osi_address(
                        {i, j, k}, {e[q][0], e[q][1], e[q][2]},
                        coarse_fab, coarse_shift);
                    dst(raw.x, raw.y, raw.z, q) = src(i, j, k, q);
                });
        }
    }
    amrex::Gpu::streamSynchronize();
}

void AmrCoreLBM::CommunicateOsiLevel(int lev) {
    MultiFab& state = osi_state.at(lev);
    MultiFab canonical(state.boxArray(), state.DistributionMap(),
                       Q, state.nGrowVect());
    const std::uint64_t phase = osi_phase.at(lev);
    const Periodicity& periodicity = geom[lev].periodicity();
    const IntVect ng = state.nGrowVect();

    // 回退路径的 tag 只能指向本次调用的临时 canonical，不能缓存其地址。
    Vector<OSI::CommunicationTag> decode_tags;
    Vector<OSI::CommunicationTag> encode_tags;
    const BoxArray& ba = state.boxArray();
    for (MFIter mfi(state, false); mfi.isValid(); ++mfi) {
        const int ibox = mfi.index();
        const Box ring_box = amrex::grow(ba[ibox], ng);
        const auto fab = OSI::MakeOsiFabContext(ring_box, phase).fab;
        const auto state_src = state.const_array(ibox);
        const auto state_dst = state.array(ibox);
        const auto canonical_src = canonical.const_array(ibox);
        const auto canonical_dst = canonical.array(ibox);
        for (const Box& bx : osi_decode_boxes.at(lev).at(ibox)) {
            decode_tags.push_back({state_src, canonical_dst, bx, fab});
        }
        for (const Box& bx : osi_encode_boxes.at(lev).at(ibox)) {
            encode_tags.push_back({canonical_src, state_dst, bx, fab});
        }
    }
    TagVector<OSI::CommunicationTag> decode_tv;
    TagVector<OSI::CommunicationTag> encode_tv;
    decode_tv.define(decode_tags);
    encode_tv.define(encode_tags);

    // 回退路径先将 OSI raw valid 解码到完整 canonical staging，再由 AMReX
    // FillBoundary 处理同 rank、跨 rank 和周期像，最后只编码回 ghost。
    {
        ScopedPerfTimer timer(perf_stats.osi_decode);
        amrex::ParallelFor(
            decode_tv, Q,
            [=] AMREX_GPU_DEVICE(
                int i, int j, int k, int q,
                const OSI::CommunicationTag& tag) noexcept {
                const auto phase_shift = OSI::osi_phase_shift(phase, tag.fab);
                const auto raw = OSI::osi_address(
                    {i, j, k}, {e[q][0], e[q][1], e[q][2]}, tag.fab,
                    phase_shift);
                tag.dst(i, j, k, q) = tag.src(raw.x, raw.y, raw.z, q);
            });
    }

    {
        ScopedPerfTimer timer(perf_stats.osi_fillboundary);
        canonical.FillBoundary(0, Q, ng, periodicity);
    }

    {
        ScopedPerfTimer timer(perf_stats.osi_encode);
        amrex::ParallelFor(
            encode_tv, Q,
            [=] AMREX_GPU_DEVICE(
                int i, int j, int k, int q,
                const OSI::CommunicationTag& tag) noexcept {
                const auto phase_shift = OSI::osi_phase_shift(phase, tag.fab);
                const auto raw = OSI::osi_address(
                    {i, j, k}, {e[q][0], e[q][1], e[q][2]}, tag.fab,
                    phase_shift);
                tag.dst(raw.x, raw.y, raw.z, q) =
                    tag.src(i, j, k, q);
            });
    }
}

void AmrCoreLBM::BuildOsiCommunicationRegionCache(int lev) {
    amrex::MultiFab& state = osi_state.at(lev);
    const BoxArray& ba = osi_state.at(lev).boxArray();
    const IntVect ng = osi_state.at(lev).nGrowVect();
    const std::uint64_t phase = osi_phase.at(lev);
    const auto shifts = Geom(lev).periodicity().shiftIntVect(ng);

    Vector<BoxList> decode_candidates(ba.size());
    Vector<BoxList> encode_candidates(ba.size());

    // 在目标坐标空间枚举 grown ghost，再将周期平移反向施加到查询 Box，
    // 从原始 BoxArray 中找到 FillBoundary 的 valid source。
    for (int dst = 0; dst < ba.size(); ++dst) {
        const Box& dst_valid = ba[dst];
        const BoxList ghost_pieces =
            amrex::boxDiff(amrex::grow(dst_valid, ng), dst_valid);

        // 并分别记录 Decode 应读取的源区域、Encode 应回写的目标 ghost 区域。
        for (const Box& dst_ghost : ghost_pieces) {
            for (const IntVect& shift : shifts) { // 采用这样的循环偏向通用性和实现可靠性。性能上它通常不是问题，因为这是一次性缓存构建，而且最多只检查少量周期像, 同时代码简洁.
                const Box source_query = dst_ghost - shift;
                for (const auto& [src, exact_source] :
                     ba.intersections(source_query)) {
                    const Box destination_box = exact_source + shift; // 一块dst_ghost可能对应多个source_box，而每个source_box对应的destination_box可能不同，所以需要用这种方式来得到destination_box

                    AMREX_ALWAYS_ASSERT(ba[src].contains(exact_source));
                    AMREX_ALWAYS_ASSERT(dst_ghost.contains(destination_box));
                    AMREX_ALWAYS_ASSERT(!(destination_box & dst_valid).ok());

                    decode_candidates[src].push_back(exact_source);
                    encode_candidates[dst].push_back(destination_box);
                }
            }
        }
    }

    auto& decode = osi_decode_boxes.at(lev);
    auto& encode = osi_encode_boxes.at(lev);
    decode.assign(ba.size(), {});
    encode.assign(ba.size(), {});

    Long decode_cells = 0;
    Long encode_cells = 0;
    Long decode_box_count = 0;
    Long encode_box_count = 0;
    Long full_decode_cells = ba.numPts(); // 获取总cell数
    Long full_encode_cells = 0;

    for (int ibox = 0; ibox < ba.size(); ++ibox) {
        BoxList disjoint_decode = amrex::removeOverlap(decode_candidates[ibox]);
        BoxList disjoint_encode = amrex::removeOverlap(encode_candidates[ibox]);
        disjoint_decode.simplify(true); // 尽力合并相邻且可以合并的 Box，减少小区域数量和后续 GPU 工作项数
        disjoint_encode.simplify(true);
        AMREX_ALWAYS_ASSERT(disjoint_decode.isDisjoint()); // 验证最终列表中的Box两两不重叠
        AMREX_ALWAYS_ASSERT(disjoint_encode.isDisjoint());

        decode[ibox].assign(disjoint_decode.begin(), disjoint_decode.end());
        encode[ibox].assign(disjoint_encode.begin(), disjoint_encode.end());

        // 主要是做了一些统计工作
        decode_box_count += static_cast<Long>(decode[ibox].size());
        encode_box_count += static_cast<Long>(encode[ibox].size());
        full_encode_cells += amrex::grow(ba[ibox], ng).numPts();
        for (const Box& bx : decode[ibox]) {
            decode_cells += bx.numPts();
        }
        for (const Box& bx : encode[ibox]) {
            encode_cells += bx.numPts();
        }
    }

    // 回退通信所需的 canonical tag 在 CommunicateOsiLevel 中随临时
    // MultiFab 一起构造，避免缓存已经失效的 Array4 地址。

    // 直接复用 AMReX FillBoundary 已缓存的通信计划；OSI 只负责 raw 地址投影。
    Vector<OSI::LocalCopyTag> local_tags;
    Vector<OSI::RemoteCopyTag> remote_tags;
    const auto fab_geometry = [&](int index) {
        const Box ring = amrex::grow(ba[index], ng);
        return OSI::MakeOsiFabContext(ring, phase).fab;
    };
    const auto& fb = state.getFB(ng, Geom(lev).periodicity());
    AMREX_ALWAYS_ASSERT(fb.m_LocTags && fb.m_SndTags && fb.m_RcvTags);
    local_tags.reserve(fb.m_LocTags->size());
    for (const auto& tag : *fb.m_LocTags) {
        local_tags.push_back({state.const_array(tag.srcIndex),
                              state.array(tag.dstIndex), tag.sbox, tag.dbox,
                              fab_geometry(tag.srcIndex),
                              fab_geometry(tag.dstIndex)});
    }

    const int my_rank = ParallelDescriptor::MyProc();
    for (const auto& [peer, tags] : *fb.m_SndTags) { // peer表示目标MPI rank
        for (const auto& tag : tags) {
            remote_tags.push_back(
                {my_rank, peer, tag.srcIndex, tag.dstIndex, tag.sbox, tag.dbox,
                 tag.dbox.smallEnd() - tag.sbox.smallEnd()});
        }
    }
    for (const auto& [peer, tags] : *fb.m_RcvTags) {
        for (const auto& tag : tags) {
            remote_tags.push_back(
                {peer, my_rank, tag.srcIndex, tag.dstIndex, tag.sbox, tag.dbox,
                 tag.dbox.smallEnd() - tag.sbox.smallEnd()});
        }
    }
    osi_local_copy_tags.at(lev).define(local_tags);
    osi_remote_copy_tags.at(lev) = std::move(remote_tags);

    // 将本 rank 的远端配对按 peer 排列，并一次性建立融合 GPU tag 与 staging 缓冲。
    const int nprocs = ParallelDescriptor::NProcs();
    auto& send_offsets = osi_mpi_send_offsets.at(lev); // 发给 peer 的数据在总发送缓冲中的起点
    auto& recv_offsets = osi_mpi_recv_offsets.at(lev); // 从 peer 收到的数据在总接收缓冲中的起点
    auto& send_counts = osi_mpi_send_counts.at(lev);   // 发给 peer 的 Real 元素总数
    auto& recv_counts = osi_mpi_recv_counts.at(lev);   // 从 peer 接收的 Real 元素总数
    send_offsets.assign(nprocs, 0);
    recv_offsets.assign(nprocs, 0);
    send_counts.assign(nprocs, 0);
    recv_counts.assign(nprocs, 0);
    for (const auto& tag : osi_remote_copy_tags.at(lev)) {
        const std::size_t values =
            static_cast<std::size_t>(tag.src_box.numPts()) * Q;
        if (tag.src_rank == my_rank) {
            send_counts[tag.dst_rank] += values;
        }
        if (tag.dst_rank == my_rank) {
            recv_counts[tag.src_rank] += values;
        }
    }
    std::size_t send_total = 0;
    std::size_t recv_total = 0;
    for (int peer = 0; peer < nprocs; ++peer) {
        send_offsets[peer] = send_total;
        recv_offsets[peer] = recv_total;
        send_total += send_counts[peer];
        recv_total += recv_counts[peer];
    }
    Vector<std::size_t> send_cursor = send_offsets;
    Vector<std::size_t> recv_cursor = recv_offsets;
    Vector<OSI::MpiPackTag> pack_tags;
    Vector<OSI::MpiUnpackTag> unpack_tags;
    for (const auto& tag : osi_remote_copy_tags.at(lev)) {
        if (tag.src_rank == my_rank) {
            const Box ring = amrex::grow(ba[tag.src_index], ng);
            const auto lo = ring.smallEnd();
            pack_tags.push_back({state.const_array(tag.src_index), tag.src_box, {{lo[0], lo[1], lo[2]}, {ring.length(0), ring.length(1), ring.length(2)}}, send_cursor[tag.dst_rank]});
            send_cursor[tag.dst_rank] +=
                static_cast<std::size_t>(tag.src_box.numPts()) * Q;
        }
        if (tag.dst_rank == my_rank) {
            const Box ring = amrex::grow(ba[tag.dst_index], ng);
            const auto lo = ring.smallEnd();
            unpack_tags.push_back({state.array(tag.dst_index), tag.dst_box, {{lo[0], lo[1], lo[2]}, {ring.length(0), ring.length(1), ring.length(2)}}, recv_cursor[tag.src_rank]});
            recv_cursor[tag.src_rank] +=
                static_cast<std::size_t>(tag.dst_box.numPts()) * Q;
        }
    }
    osi_mpi_pack_tags.at(lev).define(pack_tags);
    osi_mpi_unpack_tags.at(lev).define(unpack_tags);
    osi_mpi_send_device.at(lev).resize(send_total);
    osi_mpi_recv_device.at(lev).resize(recv_total);
    if (osi_mpi_device_direct) {
        osi_mpi_send_host.at(lev).clear();
        osi_mpi_recv_host.at(lev).clear();
    } else {
        osi_mpi_send_host.at(lev).resize(send_total);
        osi_mpi_recv_host.at(lev).resize(recv_total);
    }

    int send_peers = 0;
    int recv_peers = 0;
    for (int peer = 0; peer < nprocs; ++peer) {
        send_peers += send_counts[peer] != 0;
        recv_peers += recv_counts[peer] != 0;
    }
    amrex::Print(amrex::Print::AllProcs)
        << "[OSI mpi layout] rank=" << my_rank << " level=" << lev
        << " send_peers=" << send_peers << " recv_peers=" << recv_peers
        << " send_bytes=" << send_total * sizeof(Real)
        << " recv_bytes=" << recv_total * sizeof(Real) << '\n';

    amrex::Print() << "[OSI comm cache] level=" << lev
                   << " decode_boxes=" << decode_box_count
                   << " decode_cells=" << decode_cells
                   << " full_decode_cells=" << full_decode_cells
                   << " encode_boxes=" << encode_box_count
                   << " encode_cells=" << encode_cells
                   << " amrex_plan=1"
                   << " remote_records=" << osi_remote_copy_tags.at(lev).size()
                   << " full_encode_cells=" << full_encode_cells << '\n';
}

void AmrCoreLBM::CommunicateOsiLevelLocalDirect(int lev) {
    // 该函数也可能被其他诊断入口直接调用；跨 rank 且未打开
    // mpi-direct 时必须回到 canonical FillBoundary 路径，避免开关失效。
    if (ParallelDescriptor::NProcs() > 1 && !osi_mpi_direct) {
        CommunicateOsiLevel(lev);
        return;
    }

    ScopedPerfTimer timer(perf_stats.osi_fillboundary);
    MultiFab& state = osi_state.at(lev);
    const BoxArray& ba = state.boxArray();
    const IntVect ng = state.nGrowVect();
    const std::uint64_t phase = osi_phase.at(lev);

    // 多 rank 时在投递远端 MPI 后启动本地 seam copy，使 GPU 工作与 MPI wait 重叠。
    const auto launch_local_copy = [&]() {
        amrex::ParallelFor(
            osi_local_copy_tags.at(lev),
            [=] AMREX_GPU_DEVICE(int i, int j, int k,
                                 const OSI::LocalCopyTag& tag) noexcept {
                const auto source = OSI::source_cell(tag, i, j, k);
                const int si = source[0];
                const int sj = source[1];
                const int sk = source[2];
                const auto src_shift = OSI::osi_phase_shift(phase, tag.src_fab);
                const auto dst_shift = OSI::osi_phase_shift(phase, tag.dst_fab);
                for (int q = 0; q < Q; ++q) {
                    const auto src_raw = OSI::osi_address(
                        {si, sj, sk}, {e[q][0], e[q][1], e[q][2]},
                        tag.src_fab, src_shift);
                    const auto dst_raw = OSI::osi_address(
                        {i, j, k}, {e[q][0], e[q][1], e[q][2]},
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
    const auto& send_offsets = osi_mpi_send_offsets.at(lev);
    const auto& recv_offsets = osi_mpi_recv_offsets.at(lev);
    const auto& send_counts = osi_mpi_send_counts.at(lev);
    const auto& recv_counts = osi_mpi_recv_counts.at(lev);
    auto& send_device = osi_mpi_send_device.at(lev);
    auto& recv_device = osi_mpi_recv_device.at(lev);
    auto& send_host = osi_mpi_send_host.at(lev);
    auto& recv_host = osi_mpi_recv_host.at(lev);
    const bool pipeline_staging = osi_mpi_pipeline_chunk_bytes > 0;
    const std::size_t pipeline_chunk_reals = pipeline_staging
                                                 ? std::max<std::size_t>(
                                                       1, static_cast<std::size_t>(osi_mpi_pipeline_chunk_bytes) /
                                                              sizeof(Real))
                                                 : 0;
    bool local_copy_launched = false;

    {
        ScopedPerfTimer pack_timer(perf_stats.osi_mpi_pack);
        {
            ScopedPerfTimer kernel_timer(perf_stats.osi_mpi_pack_kernel);
            Real* buffer = send_device.data();
            amrex::ParallelFor(
                osi_mpi_pack_tags.at(lev), Q,
                [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                                     const OSI::MpiPackTag& tag) noexcept {
                    const auto lo = tag.region.smallEnd();
                    const int nx = tag.region.length(0);
                    const int ny = tag.region.length(1);
                    const std::size_t cell = static_cast<std::size_t>(
                        ((k - lo[2]) * ny + (j - lo[1])) * nx + (i - lo[0]));
                    const auto phase_shift = OSI::osi_phase_shift(phase, tag.fab);
                    const auto raw = OSI::osi_address(
                        {i, j, k}, {e[q][0], e[q][1], e[q][2]}, tag.fab,
                        phase_shift);
                    buffer[tag.offset + cell * Q + q] =
                        tag.src(raw.x, raw.y, raw.z, q);
                });
        }
        // D2H staging 在 stream 1 上提交；先明确等待 pack 所在的当前
        // stream，避免 stream 1 在 pack 尚未完成时读取 send_device。
        Gpu::streamSynchronize();
        if (!osi_mpi_device_direct && !pipeline_staging &&
            !send_device.empty()) {
            if (osi_mpi_async_staging) {
                const double dtoh_start = amrex::second();
                // stream 1 只承载 staging copy；stream 0 继续执行本地 seam copy。
                Gpu::Device::setStreamIndex(1);
                Gpu::dtoh_memcpy_async(send_host.data(), send_device.data(),
                                       send_device.size() * sizeof(Real));
                Gpu::Device::resetStreamIndex();
                launch_local_copy();
                local_copy_launched = true;
                Gpu::Device::setStreamIndex(1);
                Gpu::streamSynchronize();
                Gpu::Device::resetStreamIndex();
                perf_stats.osi_mpi_dtoh += amrex::second() - dtoh_start;
            } else {
                ScopedPerfTimer dtoh_timer(perf_stats.osi_mpi_dtoh);
                Gpu::dtoh_memcpy(send_host.data(), send_device.data(),
                                 send_device.size() * sizeof(Real));
            }
        }
    }

    {
        ScopedPerfTimer wait_timer(perf_stats.osi_mpi_wait);
        const int mpi_tag = ParallelDescriptor::SeqNum();
        Vector<ParallelDescriptor::Message> receives;
        Vector<ParallelDescriptor::Message> sends;
        Vector<std::size_t> recv_chunk_offsets;
        Vector<std::size_t> recv_chunk_counts;
        receives.reserve(nprocs);
        sends.reserve(nprocs);
        for (int peer = 0; peer < nprocs; ++peer) {
            if (pipeline_staging) {
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
            } else if (recv_counts[peer] != 0) {
                Real* recv_buffer = osi_mpi_device_direct
                                        ? recv_device.data()
                                        : recv_host.data();
                receives.push_back(ParallelDescriptor::Arecv(
                    recv_buffer + recv_offsets[peer], recv_counts[peer],
                    peer, mpi_tag));
            }
        }
        if (pipeline_staging && !local_copy_launched) {
            launch_local_copy();
            local_copy_launched = true;
        }
        for (int peer = 0; peer < nprocs; ++peer) {
            if (pipeline_staging) {
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
                    perf_stats.osi_mpi_dtoh += amrex::second() - dtoh_start;
                    sends.push_back(ParallelDescriptor::Asend(
                        send_host.data() + offset, count, peer, mpi_tag));
                }
            } else if (send_counts[peer] != 0) {
                const Real* send_buffer = osi_mpi_device_direct
                                              ? send_device.data()
                                              : send_host.data();
                sends.push_back(ParallelDescriptor::Asend(
                    send_buffer + send_offsets[peer], send_counts[peer],
                    peer, mpi_tag));
            }
        }
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
            perf_stats.osi_mpi_htod += amrex::second() - pipeline_htod_start;
        }
        if (osi_mpi_async_staging && !recv_device.empty()) {
            Gpu::Device::setStreamIndex(1);
            Gpu::streamSynchronize();
            Gpu::Device::resetStreamIndex();
            perf_stats.osi_mpi_htod += amrex::second() - htod_start;
        }
    }

    {
        ScopedPerfTimer unpack_timer(perf_stats.osi_mpi_unpack);
        if (!osi_mpi_device_direct && !osi_mpi_async_staging &&
            !pipeline_staging &&
            !recv_device.empty()) {
            ScopedPerfTimer htod_timer(perf_stats.osi_mpi_htod);
            Gpu::htod_memcpy(recv_device.data(), recv_host.data(),
                             recv_device.size() * sizeof(Real));
        }
        {
            ScopedPerfTimer kernel_timer(perf_stats.osi_mpi_unpack_kernel);
            const Real* buffer = recv_device.data();
            amrex::ParallelFor(
                osi_mpi_unpack_tags.at(lev), Q,
                [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                                     const OSI::MpiUnpackTag& tag) noexcept {
                    const auto lo = tag.region.smallEnd();
                    const int nx = tag.region.length(0);
                    const int ny = tag.region.length(1);
                    const std::size_t cell = static_cast<std::size_t>(
                        ((k - lo[2]) * ny + (j - lo[1])) * nx + (i - lo[0]));
                    const auto phase_shift = OSI::osi_phase_shift(phase, tag.fab);
                    const auto raw = OSI::osi_address(
                        {i, j, k}, {e[q][0], e[q][1], e[q][2]}, tag.fab,
                        phase_shift);
                    tag.dst(raw.x, raw.y, raw.z, q) =
                        buffer[tag.offset + cell * Q + q];
                });
        }
    }
#else
    amrex::Abort("lbm.osi_mpi_direct requires an MPI build");
#endif
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
                              {{source_lo[0], source_lo[1], source_lo[2]},
                               {source_ring.length(0), source_ring.length(1),
                                source_ring.length(2)}},
                              {{destination_lo[0], destination_lo[1], destination_lo[2]},
                               {destination_ring.length(0), destination_ring.length(1),
                                destination_ring.length(2)}}});
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
                {source[0], source[1], source[2]},
                {e[q][0], e[q][1], e[q][2]}, tag.src_fab, source_shift);
            const auto destination_shift =
                OSI::osi_phase_shift(destination_phase, tag.dst_fab);
            const auto destination_raw = OSI::osi_address(
                {i, j, k}, {e[q][0], e[q][1], e[q][2]},
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
            pack_tags.push_back({source.const_array(tag.srcIndex), tag.sbox, {{source_lo[0], source_lo[1], source_lo[2]}, {source_ring.length(0), source_ring.length(1), source_ring.length(2)}}, send_cursor[peer]});
            send_cursor[peer] +=
                static_cast<std::size_t>(tag.sbox.numPts()) * Q;
        }
    }

    for (const auto& [peer, tags] : *cpc.m_RcvTags) {
        for (const auto& tag : tags) {
            const Box destination_ring = amrex::grow(
                destination.boxArray()[tag.dstIndex], destination.nGrowVect());
            const auto destination_lo = destination_ring.smallEnd();
            unpack_tags.push_back({destination.array(tag.dstIndex), tag.dbox, {{destination_lo[0], destination_lo[1], destination_lo[2]}, {destination_ring.length(0), destination_ring.length(1), destination_ring.length(2)}}, recv_cursor[peer]});
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
                ((k - lo[2]) * ny + (j - lo[1])) * nx + (i - lo[0]));
            const auto shift = OSI::osi_phase_shift(source_phase, tag.fab);
            const auto raw = OSI::osi_address(
                {i, j, k}, {e[q][0], e[q][1], e[q][2]}, tag.fab, shift);
            send_buffer[tag.offset + cell * Q + q] =
                tag.src(raw.x, raw.y, raw.z, q);
        },
        [=] AMREX_GPU_DEVICE(int i, int j, int k, int q,
                             const OSI::MpiUnpackTag& tag,
                             const Real* receive_buffer) noexcept {
            const auto lo = tag.region.smallEnd();
            const int nx = tag.region.length(0);
            const int ny = tag.region.length(1);
            const std::size_t cell = static_cast<std::size_t>(
                ((k - lo[2]) * ny + (j - lo[1])) * nx + (i - lo[0]));
            const auto shift =
                OSI::osi_phase_shift(destination_phase, tag.fab);
            const auto raw = OSI::osi_address(
                {i, j, k}, {e[q][0], e[q][1], e[q][2]}, tag.fab, shift);
            tag.dst(raw.x, raw.y, raw.z, q) =
                receive_buffer[tag.offset + cell * Q + q];
        });
}

void AmrCoreLBM::ScaleCanonical(
    const MultiFab& density_mf, const MultiFab& velocity_mf,
    MultiFab& canonical, Real scale) const {
    AMREX_ALWAYS_ASSERT(density_mf.boxArray() == canonical.boxArray());
    AMREX_ALWAYS_ASSERT(velocity_mf.boxArray() == canonical.boxArray());
    AMREX_ALWAYS_ASSERT(density_mf.DistributionMap() == canonical.DistributionMap());
    AMREX_ALWAYS_ASSERT(velocity_mf.DistributionMap() == canonical.DistributionMap());
    AMREX_ALWAYS_ASSERT(canonical.nComp() == Q);

    for (MFIter mfi(canonical, false); mfi.isValid(); ++mfi) {
        const Box bx = mfi.validbox();
        const auto rho_field = density_mf.const_array(mfi);
        const auto velocity_field = velocity_mf.const_array(mfi);
        const auto ddf = canonical.array(mfi);
        amrex::ParallelFor(
            bx, Q,
            [=] AMREX_GPU_DEVICE(int i, int j, int k, int q) noexcept {
                const Real rho = rho_field(i, j, k);
                const Real ux = velocity_field(i, j, k, 0);
                const Real uy = velocity_field(i, j, k, 1);
                const Real uz = velocity_field(i, j, k, 2);
                const Real feq = feqQian(rho, {ux, uy, uz}, q);
                ddf(i, j, k, q) =
                    feq + (ddf(i, j, k, q) - feq) * scale;
            });
    }
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
                    {i, j, k}, {e[q][0], e[q][1], e[q][2]}, fab,
                    phase_shift);
                dst(i, j, k, q) = src(raw.x, raw.y, raw.z, q);
            });
    }
}

void AmrCoreLBM::InterpolateOsiFinePatchFromCoarse(
    int lev, Real time, const BoxArray& fine_patch_ba,
    const DistributionMapping& patch_dm, MultiFab& destination) {
    if (fine_patch_ba.empty()) {
        return;
    }
    AMREX_ALWAYS_ASSERT(lev > 0);
    AMREX_ALWAYS_ASSERT(refRatio(lev - 1) == IntVect(2));
    const IntVect ratio = refRatio(lev - 1);
    const auto coarsener = DdfInterpolater()->BoxCoarsener(ratio);
    Vector<Box> coarse_boxes;
    coarse_boxes.reserve(fine_patch_ba.size());
    for (int ibox = 0; ibox < fine_patch_ba.size(); ++ibox) {
        coarse_boxes.push_back(coarsener.doit(fine_patch_ba[ibox]));
    }

    BoxArray coarse_ba(
        coarse_boxes.data(), static_cast<int>(coarse_boxes.size()));
    MultiFab coarse_patch(coarse_ba, patch_dm, Q, 0);
    coarse_patch.setDomainBndry(
        std::numeric_limits<Real>::quiet_NaN(), Geom(lev - 1));

    const MultiFab& coarse_state = osi_state.at(lev - 1);
    const auto coarse_phase = osi_phase.at(lev - 1);
    // 稀疏粗层 patch 也直接使用 phase-aware 完整 Q 复制，避免重复分批解码。
    ParallelCopyOsi(lev - 1, coarse_state, coarse_phase, coarse_patch, 0);

    if (Gpu::inLaunchRegion()) { // 处理 coarse 物理边界
        GpuBndryFuncFab<AmrCoreFill> gpu_bndry_func(AmrCoreFill{});
        PhysBCFunct<GpuBndryFuncFab<AmrCoreFill>> cphysbc(
            geom[lev - 1], bcs, gpu_bndry_func);
        cphysbc(coarse_patch, 0, Q, coarse_patch.nGrowVect(), time, 0);
    } else {
        CpuBndryFuncFab bndry_func(nullptr);
        PhysBCFunct<CpuBndryFuncFab> cphysbc(
            geom[lev - 1], bcs, bndry_func);
        cphysbc(coarse_patch, 0, Q, coarse_patch.nGrowVect(), time, 0);
    }

    const Real scale = tau.at(lev) / tau.at(lev - 1) / Real(2.0);
    for (MFIter mfi(coarse_patch, false); mfi.isValid(); ++mfi) {
        const Box bx = mfi.validbox();
        const auto coarse = coarse_patch.array(mfi);
        amrex::ParallelFor(
            bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {
                average_scale(i, j, k, coarse, scale);
            });
    }

    // regrid 只为新增 fine valid 区构造稀疏 patch。destination 是完整
    // phase-0 level，二者布局不同，因此始终通过 sparse patch 中转。
    MultiFab fine_patch(fine_patch_ba, patch_dm, Q, 0);

    for (MFIter mfi(fine_patch, false); mfi.isValid(); ++mfi) {
        const Box bx = mfi.validbox();
        const auto fine = fine_patch.array(mfi);
        const auto coarse = coarse_patch.const_array(mfi);
        if (interp_mode == 0) {
            amrex::ParallelFor(
                bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {
                    interp_bilinear_d3q(i, j, k, fine, coarse);
                });
        } else {
            const Box coarse_parent_box = amrex::coarsen(bx, 2);
            if (interp_mode == 1) {
                amrex::ParallelFor(
                    coarse_parent_box,
                    [=] AMREX_GPU_DEVICE(int ic, int jc, int kc) noexcept {
                        interp_cell_cons_linear_children_d3q(
                            ic, jc, kc, fine, coarse, bx);
                    });
            } else {
                amrex::ParallelFor(
                    coarse_parent_box,
                    [=] AMREX_GPU_DEVICE(int ic, int jc, int kc) noexcept {
                        interp_cell_quadratic_children_d3q(
                            ic, jc, kc, fine, coarse, bx);
                    });
            }
        }
    }

    destination.ParallelCopy(
        fine_patch, 0, 0, Q, IntVect(0), IntVect(0),
        Geom(lev).periodicity());
}
