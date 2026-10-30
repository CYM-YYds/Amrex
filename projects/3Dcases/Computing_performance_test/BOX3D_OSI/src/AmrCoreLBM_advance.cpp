#include "AmrCoreLBM.H"

#include <AMReX_MultiFabUtil.H>
#include <AMReX_Utility.H>
#include "Kernels.H"

using namespace amrex;
using namespace Box3dDetail;

// 生产推进阶段与数据路径分派。

void AmrCoreLBM::ComputeVorticityLevel(int lev) {

    const amrex::MultiFab& level_layout =
        streamMode() == 1 ? osi_state.at(lev) : f_old.at(lev);
    amrex::MultiFab& u_lev = velocity[lev];
    amrex::MultiFab& vort_lev = vorticity[lev];
    amrex::Real dt = Geom(lev).CellSizeArray()[0];

    for (MFIter mfi(level_layout, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const Box& bx = mfi.growntilebox(0);

        Array4<Real> const& u = u_lev.array(mfi);
        Array4<Real> const& vort = vort_lev.array(mfi);

        amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
            compute_vorticity(i, j, k, u, vort, dt);
        });
    }
}

void AmrCoreLBM::ComputeVorticity(amrex::Real cur_time) {
    for (int lev = 0; lev <= finest_level; lev++) {
        FillMacroGhostLevel(lev, cur_time);
        ComputeVorticityLevel(lev);
    }
}

void AmrCoreLBM::ComputeShearLevel(int lev) {
    const amrex::MultiFab& level_layout =
        streamMode() == 1 ? osi_state.at(lev) : f_old.at(lev);
    amrex::MultiFab& u_lev = velocity[lev];
    amrex::MultiFab& shear_lev = shear[lev];
    amrex::Real dt = Geom(lev).CellSizeArray()[0];

    for (MFIter mfi(level_layout, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const Box& bx = mfi.growntilebox(0);

        Array4<Real> const& u = u_lev.array(mfi);
        Array4<Real> const& shear = shear_lev.array(mfi);

        amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
#if AMREX_SPACEDIM == 3
            compute_shear(i, j, k, u, shear, dt);
#else
            shear(i, j, k, 0) = 0;
#endif
        });
    }
}

void AmrCoreLBM::ComputeShear() {
    for (int lev = 0; lev <= finest_level; lev++) {
        ComputeShearLevel(lev);
    }
}

void AmrCoreLBM::AverageDownValidLevel(int lev, bool is_scale, DdfLayout layout) {
    AMREX_ALWAYS_ASSERT(lev >= 0 && lev < finest_level);
    const bool use_osi = layout == DdfLayout::Osi;
    const int fine_lev = lev + 1;
    const auto& fine = use_osi ? osi_state.at(fine_lev) : f_old.at(fine_lev);
    const auto ratio = refRatio(lev);
    AMREX_ALWAYS_ASSERT(ratio == IntVect(2) && fine.boxArray().coarsenable(ratio));
    // 完整 valid 的父单元与 fine Fab 一一对应；布局与任务只在本次调用内有效。
    MultiFab result(amrex::coarsen(fine.boxArray(), ratio), fine.DistributionMap(), Q, 0);
    Vector<int> fine_indices(fine.boxArray().size());
    for (int index = 0; index < fine_indices.size(); ++index) {
        fine_indices[index] = index;
    }
    Gpu::DeviceVector<RestrictionBlockTag> work;
    BuildRestrictionWork(fine, result, fine_indices, work,
                         use_osi ? &osi_fab_phase_cache.at(fine_lev) : nullptr);
    OSI::CpcCache<false> copy;
    if (use_osi) {
        RestrictDdfToCoarse<true>(fine_lev, result, fine_indices, work, is_scale);
        copy.define(result, osi_state.at(lev), Geom(lev).periodicity(), Q,
                    osi_mpi_device_direct, osi_fab_phase_cache.at(lev));
        ParallelCopyOsi(copy, osi_fab_phase_cache.at(lev).data(osi_phase.at(lev)));
    } else {
        // f_new 是 AB 的下一次交换目标，保留旧完整平均对其全部 fine valid 的写入。
        RestrictDdfToCoarse<false, true>(fine_lev, result, fine_indices, work, is_scale);
        f_old.at(lev).ParallelCopy(result, 0, 0, Q);
    }
    // 临时设备任务、结果与 MPI 搬运完成后再离开其作用域。
    Gpu::streamSynchronize();
}

void AmrCoreLBM::AverageDownValid(const std::function<void()>& before_repair) {
    // interface-only 时间推进不会更新深层 covered 粗单元；regrid 可能重新暴露这些单元，
    // 因此重网格前必须先将全部细层 valid 数据完整同步到粗层父单元。
    for (int lev = finest_level - 1; lev >= 0; --lev) {
        if (streamMode() == 1) {
            AverageDownValidLevel(lev, true, DdfLayout::Osi);
            if (osiReferenceEnabled()) {
                // 锁步参考态也要在重网格前完成全部有效单元的平均下传。
                AverageDownValidLevel(lev, true);
            }
        } else {
            AverageDownValidLevel(lev, true);
        }
    }
    // 保留平均后、边界修复前的诊断阶段；单层网格没有平均写入区域。
    if (finest_level > 0 && before_repair) {
        before_repair();
    }
    RepairCurrentStatePhysicalBoundary();
}

void AmrCoreLBM::RepairCurrentStatePhysicalBoundary() { // 在平均后，修复当前状态的物理边界条件。
    ScopedPerfTimer timer(perf_stats.boundary, !useUniformReducedSync());
    const DdfLayout layout =
        streamMode() == 1 ? DdfLayout::Osi : DdfLayout::Canonical;

    // 平均后只修复 covered 粗层物理边界，不改写 uncovered 或最细层状态。
    for (int lev = 0; lev < finest_level; ++lev) {
        amrex::MultiFab& state =
            layout == DdfLayout::Osi ? osi_state.at(lev) : f_old.at(lev);
        ApplyPhysicalBoundaryLevel(lev, state, layout, true);
        if (osiReferenceEnabled() && layout == DdfLayout::Osi) {
            // 锁步参考态使用与 OSI 当前态相同的 covered 修复范围。
            ApplyPhysicalBoundaryLevel(
                lev, f_old.at(lev), DdfLayout::Canonical, true);
        }
    }
}

template <bool UseOsiAddress, bool StoreScaledFine>
void AmrCoreLBM::RestrictDdfToCoarse(
    int fine_lev, MultiFab& result, const Vector<int>& fine_indices,
    const Gpu::DeviceVector<RestrictionBlockTag>& work, bool is_scale) {
    const auto& fine_state = UseOsiAddress ? osi_state.at(fine_lev) : f_old.at(fine_lev);
    const auto ratio = refRatio(fine_lev - 1);
    const Real scale = Real(2.0) * tau.at(fine_lev - 1) / tau.at(fine_lev);
    AMREX_ALWAYS_ASSERT(result.size() == fine_indices.size());
    const OSI::PhaseShift3* shifts = nullptr;
    if constexpr (UseOsiAddress) {
        shifts = osi_fab_phase_cache.at(fine_lev).data(osi_phase.at(fine_lev));
    }
#ifdef AMREX_USE_CUDA
    if (!work.empty()) {
        const auto* tags = work.data();
        const auto outputs = result.arrays();
        const auto fines = fine_state.const_arrays();
        const auto scaled = StoreScaledFine ? f_new.at(fine_lev).arrays() : MultiArray4<Real>{};
        amrex::launch<restriction_threads_per_block>(
            static_cast<int>(work.size()), Gpu::Device::gpuStream(),
            [=] AMREX_GPU_DEVICE() noexcept {
                const auto tag = tags[blockIdx.x];
                const Long parent = tag.first_parent + threadIdx.x / restriction_group_size;
                // 整组退出，保证同组的 shuffle 掩码与有效线程一致。
                if (parent >= tag.region.numPts()) {
                    return;
                }
                const auto lo = amrex::lbound(tag.region);
                const int nx = tag.region.length(0), ny = tag.region.length(1);
                const int i = lo.x + static_cast<int>(parent % nx);
                const Long yz = parent / nx;
                const int j = lo.y + static_cast<int>(yz % ny);
                const int k = lo.z + static_cast<int>(yz / ny);
                const auto shift = UseOsiAddress ? shifts[tag.phase_index] : OSI::PhaseShift3{};
                const auto scaled_fine = StoreScaledFine ? scaled[tag.fine_local_index] : Array4<Real>{};
#if Q == 27
                average_down_to_canonical_half_warp<UseOsiAddress, StoreScaledFine>(
                    i, j, k, threadIdx.x % restriction_group_size,
                    outputs[tag.output_local_index], fines[tag.fine_local_index],
                    tag.fab, shift, scale, is_scale, scaled_fine);
#else
                average_down_to_canonical<UseOsiAddress, StoreScaledFine>(
                    i, j, k, outputs[tag.output_local_index], fines[tag.fine_local_index],
                    ratio, tag.fab, shift, scale, is_scale, scaled_fine);
#endif
            });
    }
#else
    for (MFIter mfi(result, false); mfi.isValid(); ++mfi) {
        const int fine_index = fine_indices.at(mfi.index());
        const auto context = OSI::MakeOsiFabContext(
            amrex::grow(fine_state.boxArray()[fine_index], fine_state.nGrowVect()),
            UseOsiAddress ? osi_phase.at(fine_lev) : 0);
        const auto coarse = result.array(mfi);
        const auto fine = fine_state.const_array(fine_index);
        const auto scaled_fine = StoreScaledFine ? f_new.at(fine_lev).array(fine_index) : Array4<Real>{};
        amrex::ParallelFor(mfi.validbox(), [=] AMREX_GPU_DEVICE(int i, int j, int k) {
            average_down_to_canonical<UseOsiAddress, StoreScaledFine>(
                i, j, k, coarse, fine, ratio, context.fab, context.phase_shift,
                scale, is_scale, scaled_fine);
        });
    }
#endif
}

template <bool UseOsiAddress>
void AmrCoreLBM::AverageDownLayoutLevel(int fine_lev, bool is_scale) {
    const int coarse_lev = fine_lev - 1;
    auto& cache = average_down_cache.at(coarse_lev);
    if (cache.fine_box_indices.empty()) {
        return;
    }
    auto& interface_result = cache.interface_buffer;
    const auto ratio = refRatio(coarse_lev);
    AMREX_ALWAYS_ASSERT(interface_result.size() == cache.fine_box_indices.size());
    for (int index : interface_result.IndexArray()) {
        const Long parents = interface_result.boxArray()[index].numPts();
        perf_stats.average_parent_cells += parents;
        if (is_scale) {
            perf_stats.average_scale_cells += parents * ratio[0] * ratio[1] * AMREX_D_PICK(1, 1, ratio[2]);
        }
    }
    // 保留 A-B 既有细分计时；OSI 关闭细分时不新增阶段同步。
    const bool detail = !UseOsiAddress || params_.perf_osi_detail_timing;
    ScopedPerfTimer avgdown_timer(perf_stats.average_down, detail);
    {
        ScopedPerfTimer fused_timer(perf_stats.average_fused, detail);
        RestrictDdfToCoarse<UseOsiAddress>(
            fine_lev, interface_result, cache.fine_box_indices, cache.block_tags, is_scale);
    }
    ScopedPerfTimer copyback_timer(perf_stats.average_copyback, detail);
    if constexpr (UseOsiAddress) {
        ParallelCopyOsi(cache.copy, osi_fab_phase_cache.at(coarse_lev).data(osi_phase.at(coarse_lev)),
                        {&perf_stats.osi_mpi_pack, &perf_stats.osi_mpi_wait, &perf_stats.osi_mpi_unpack});
    } else {
        f_old.at(coarse_lev).ParallelCopy(interface_result, 0, 0, Q);
    }
}

void AmrCoreLBM::AverageDownInterfaceLevel(int fine_lev, bool is_scale) {
    const int coarse_lev = fine_lev - 1;
    AMREX_ALWAYS_ASSERT(fine_lev > 0 && fine_lev <= finest_level);
    ScopedPerfTimer timer(perf_stats.average);
    ++perf_stats.avgdown_calls;
    const bool probe_this_call = probe_.average_first && coarse_lev == 0 && is_scale &&
                                 ++probe_.level0_average_count == 1;
    if (probe_this_call) {
        ProbeAverageInterfaceLevel(coarse_lev, "before");
    }
    if (streamMode() == 1) {
        AverageDownLayoutLevel<true>(fine_lev, is_scale);
    }
    if (streamMode() == 0 || osiReferenceEnabled()) {
        AverageDownLayoutLevel<false>(fine_lev, is_scale);
    }
    if (probe_this_call) {
        ProbeAverageInterfaceLevel(coarse_lev, "after");
    }
}

void AmrCoreLBM::FillGhostLevel(int coarse_lev, amrex::Real time, bool is_scale) {
    ScopedPerfTimer timer(perf_stats.interp);
    ++perf_stats.fillghost_calls;
    const int fine_lev = coarse_lev + 1;
    AMREX_ALWAYS_ASSERT(coarse_lev >= 0 && fine_lev <= finest_level);

    if (streamMode() == 1) {
        FillDdfGhostFromCoarse(coarse_lev, time, is_scale, DdfLayout::Osi);
        if (osiReferenceEnabled()) {
            // 锁步参考态必须与独立 A-B 一样填充粗到细 ghost。
            FillDdfGhostFromCoarse(coarse_lev, time, is_scale);
        }
    } else {
        FillDdfGhostFromCoarse(coarse_lev, time, is_scale);
    }
    if (probe_.ghost_first_fill && fine_lev == 1 &&
        ++probe_.level1_fill_count == 1) {
        ProbeFillGhostLevel(fine_lev);
    }
}

void AmrCoreLBM::FillMacroGhostLevel(int lev, amrex::Real time) {
    amrex::MultiFab& u_lev = velocity[lev];

    if (lev == 0) {
        u_lev.FillBoundary(geom[lev].periodicity());
    } else {
        FillMacroPatch(lev, time, u_lev);            // 填充c-f边界
        u_lev.FillBoundary(geom[lev].periodicity()); // 填充同等级
    }
}

void AmrCoreLBM::FillForceGhostLevel(int lev, amrex::Real time) {

    amrex::MultiFab& force_lev = force[lev];

    if (lev == 0) {
        force_lev.FillBoundary(geom[lev].periodicity());
    } else {
        // 填充c-f边界
        force_lev.FillBoundary(geom[lev].periodicity()); // 填充同等级
    }
}

void AmrCoreLBM::CommunicateLevel(int lev, DdfLayout layout) {
    ScopedPerfTimer timer(perf_stats.comm, !useUniformReducedSync());
    if (layout == DdfLayout::Osi) {
        FillBoundaryOsi(lev);
        return;
    }

    amrex::MultiFab& f_old_lev = f_old[lev];
    f_old_lev.FillBoundary(geom[lev].periodicity());
}

void AmrCoreLBM::AdvanceOneLayout(int lev, DdfLayout layout) {
    // 支持路径逐步采样并累计到当前报告窗口，main按samples输出窗口平均。
    const bool sample = uniformSampleEligible() && lev == 0 && layout == DdfLayout::Osi;
    uniform_sample_active = sample;
    if (sample) { uniform_gpu_events.reset(); }
    const double sample_start = sample ? amrex::second() : 0.0;
    auto& timing = perf_stats.uniform_sample;
    const double finalize_before = timing.cpu_collide_finalize;
    double collide_cpu = 0.0, comm_cpu = 0.0, phase_cpu = 0.0, boundary_cpu = 0.0;
    // 生产推进只执行一次布局；锁步检测由 AdvanceLevel 显式编排两次布局。
    if (layout == DdfLayout::Osi && osi_collide_comm_overlap &&
        osi_fillboundary_cache.at(lev).overlap_collision) {
        Collide(lev, nghost, layout, 1);
        ScopedPerfTimer comm_timer(perf_stats.comm, !useUniformReducedSync());
        FillBoundaryOsi(lev, [&]() { Collide(lev, nghost, layout, 0); });
    } else {
        const double begin = sample ? amrex::second() : 0.0;
        Collide(lev, nghost, layout);
        const double collided = sample ? amrex::second() : 0.0;
        CommunicateLevel(lev, layout);
        if (sample) {
            collide_cpu = collided - begin;
            comm_cpu = amrex::second() - collided;
        }
    }
    const double phase_start = sample ? amrex::second() : 0.0;
    const auto phase_event = sample ? uniform_gpu_events.begin(UniformGpuEvents::Phase) : 0;
    Stream(lev, nghost, layout);
    if (sample) { uniform_gpu_events.end(phase_event); }
    const double boundary_start = sample ? amrex::second() : 0.0;
    const auto boundary_event = sample ? uniform_gpu_events.begin(UniformGpuEvents::Boundary) : 0;
    Boundary(lev, layout);
    if (sample) { uniform_gpu_events.end(boundary_event); }
    const double wait_start = sample ? amrex::second() : 0.0;
    if (useUniformReducedSync()) {
        // phase、本地复制和边界在默认 stream 有序执行，步末等待覆盖它们。
        amrex::Gpu::streamSynchronize();
    }
    if (sample) {
        const double completed = amrex::second();
        phase_cpu = boundary_start - phase_start;
        boundary_cpu = wait_start - boundary_start;
        const double wait = completed - wait_start;
        const double step = completed - sample_start;
        const auto gpu = uniform_gpu_events.seconds();
        ++timing.samples;
        timing.gpu_collide_sum += gpu[UniformGpuEvents::Collision];
        timing.gpu_phase += gpu[UniformGpuEvents::Phase];
        timing.gpu_boundary += gpu[UniformGpuEvents::Boundary];
        timing.cpu_collide_submit += collide_cpu - (timing.cpu_collide_finalize - finalize_before);
        timing.cpu_comm += comm_cpu;
        timing.cpu_phase_submit += phase_cpu;
        timing.cpu_boundary_submit += boundary_cpu;
        timing.cpu_step_wait += wait;
        timing.cpu_step += step;
        timing.cpu_other += step - collide_cpu - comm_cpu - phase_cpu - boundary_cpu - wait;
        timing.cpu_collect += amrex::second() - completed;
    }
    uniform_sample_active = false;
}

void AmrCoreLBM::AdvanceLevel(int lev) {
    if (lev == 0) {
        ++probe_.coarse_advance_count;
    }
    const DdfLayout layout =
        streamMode() == 1 ? DdfLayout::Osi : DdfLayout::Canonical;
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        layout == DdfLayout::Canonical || osi_state.at(lev).isDefined(),
        "AdvanceLevel requires an initialized OSI state");

    if (run_mode == RunMode::OsiLockstep) {
        // 诊断模式下两种布局锁步推进，阶段结束后立即定位第一处分歧。
        CompareOsiReferenceStage(lev, "Initial", f_old.at(lev), 0);
        if (lev > 0) {
            CompareOsiReferenceStage(
                lev, "InterpolationGhost", f_old.at(lev), nghost, true);
        }
        const bool overlap = osi_collide_comm_overlap &&
                             osi_fillboundary_cache.at(lev).overlap_collision;
        Collide(lev, nghost, DdfLayout::Osi, overlap ? 1 : -1);
        Collide(lev, nghost, DdfLayout::Canonical);
        if (overlap) {
            ScopedPerfTimer comm_timer(perf_stats.comm, !useUniformReducedSync());
            FillBoundaryOsi(lev, [&]() {
                Collide(lev, nghost, DdfLayout::Osi, 0);
                CompareOsiReferenceStage(lev, "Collision", f_old.at(lev), 0);
            });
        } else {
            CompareOsiReferenceStage(lev, "Collision", f_old.at(lev), 0);
            CommunicateLevel(lev, DdfLayout::Osi);
        }
        CommunicateLevel(lev, DdfLayout::Canonical);
        CompareOsiReferenceStage(
            lev, "Communication", f_old.at(lev), 0);
        CompareOsiReferenceStage(
            lev, "CommunicationGhost", f_old.at(lev), nghost, true);
        // 直接比较所有 uncovered Stream 目标所读取的 source，覆盖 valid、
        // interface 和 ghost；该检查对每个 AMR 层级分别执行。
        CompareOsiStreamSources(lev, "AfterCommunication");

        Stream(lev, nghost, DdfLayout::Osi);
        Stream(lev, nghost, DdfLayout::Canonical);
        CompareOsiReferenceStage(
            lev, "Stream", f_new.at(lev), 0, true);

        Boundary(lev, DdfLayout::Osi);
        Boundary(lev, DdfLayout::Canonical);
        // 边界处理后再次确认该层所有 uncovered valid 单元。
        CompareOsiReferenceStage(lev, "BoundaryUncovered", f_new.at(lev), 0);

        SwapLevel(lev, nghost, DdfLayout::Osi);
        SwapLevel(lev, nghost, DdfLayout::Canonical);
        CompareOsiReferenceStage(lev, "Swap", f_old.at(lev), 0);
        return;
    }

    // 生产模式只经过一次布局推进；锁步分支已在上方显式执行两种布局。
    AdvanceOneLayout(lev, layout);
    if (lev == 0 && probe_.coarse_advance_count == probe_.boundary_step) {
        ProbeBoundarySample(lev, layout);
    }
    SwapLevel(lev, nghost, layout);
}

void AmrCoreLBM::ApplyPhysicalBoundaryLevel(
    int lev, amrex::MultiFab& state_lev, DdfLayout layout,
    bool repair_after_average) {
    if (repair_after_average && lev >= finest_level) {
        return;
    }
    const bool use_osi = layout == DdfLayout::Osi;
    const Box domain = Geom(lev).Domain();
    const amrex::IntVect hi = domain.bigEnd();
    const auto is_periodic = Geom(lev).isPeriodicArray();
    const bool has_fine_level = lev < finest_level;
    const std::uint64_t phase = use_osi ? osi_phase.at(lev) : 0;

    // 索引任务与当前布局一致；数组在调用时取得，避免 canonical 交换后绑定旧存储。
    AMREX_ALWAYS_ASSERT(state_lev.boxArray() == boxArray(lev) &&
                        state_lev.DistributionMap() == DistributionMap(lev) &&
                        state_lev.nGrowVect() == IntVect(nghost));
    const auto states = state_lev.arrays();
    const auto masks = has_fine_level ? covered_mask[lev].const_arrays()
                                      : MultiArray4<const int>{};
    const auto& cache = boundary_work_cache[lev];
    const auto& tags = repair_after_average ? cache.repair_tags : cache.advance_tags;
    perf_stats.boundary_full_cells += cache.full_cells;
    perf_stats.boundary_launch_cells += repair_after_average
                                            ? cache.repair_cells
                                            : cache.advance_cells;

    if (use_osi) {
        const auto* phase_shifts = osi_fab_phase_cache.at(lev).data(phase);
        amrex::ParallelFor(
            tags, [=] AMREX_GPU_DEVICE(int i, int j, int k,
                                       const BoundaryWorkTag& tag) noexcept {
                if (has_fine_level &&
                    (repair_after_average ? masks[tag.local_index](i, j, k) == 0
                                          : masks[tag.local_index](i, j, k) != 0)) {
                    return;
                }
                fill_boundary_osi_state(
                    i, j, k, states[tag.local_index], hi, is_periodic,
                    tag.fab, phase_shifts[tag.phase_index]);
            });
    } else {
        amrex::ParallelFor(
            tags, [=] AMREX_GPU_DEVICE(int i, int j, int k,
                                       const BoundaryWorkTag& tag) noexcept {
                if (has_fine_level &&
                    (repair_after_average ? masks[tag.local_index](i, j, k) == 0
                                          : masks[tag.local_index](i, j, k) != 0)) {
                    return;
                }
                fill_boundary(i, j, k, states[tag.local_index], hi, is_periodic);
            });
    }
}

void AmrCoreLBM::Boundary(int lev, DdfLayout layout) {
    ScopedPerfTimer timer(perf_stats.boundary, !useUniformReducedSync());
    amrex::MultiFab& state =
        layout == DdfLayout::Osi ? osi_state.at(lev) : f_new.at(lev);
    ApplyPhysicalBoundaryLevel(lev, state, layout, false);
}

void AmrCoreLBM::Collide(int lev, int n, DdfLayout layout, int send_subset) {
    if (useUniformNoMaskCollision()) {
        CollideLayoutLevel<false>(lev, n, layout, send_subset);
    } else {
        CollideLayoutLevel<true>(lev, n, layout, send_subset);
    }
}

template <bool UseMask>
void AmrCoreLBM::CollideLayoutLevel(int lev, int n, DdfLayout layout, int send_subset) {
    const double collide_start = amrex::second();

    const bool use_osi = layout == DdfLayout::Osi;
    AMREX_ALWAYS_ASSERT(send_subset >= -1 && send_subset <= 1 &&
                        (send_subset == -1 || use_osi));
    const Box domain = Geom(lev).Domain();
    const amrex::IntVect hi = domain.bigEnd();
    amrex::MultiFab& state_lev =
        use_osi ? osi_state.at(lev) : f_old.at(lev);
    amrex::MultiFab& shear_lev = shear[lev];
    amrex::MultiFab& force_lev = force[lev];
    amrex::Real dt = Geom(lev).CellSizeArray()[0];
    amrex::Real tau_lev = tau[lev];
    const amrex::Real omega_lev = 1.0 / tau_lev;
    const Box collision_domain = Geom(lev).growPeriodicDomain(n);
    const bool has_fine_level = UseMask && lev < finest_level;
    const std::uint64_t phase = use_osi ? osi_phase.at(lev) : 0;
    long long level_launch_cells = 0;

    if (use_osi) {
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            collide_mode == 1,
            "OSI collision requires lbm.collide_mode=1");
    }

    AMREX_ALWAYS_ASSERT(n >= 0 && n <= state_lev.nGrow());
    // GPU 上两种布局均按完整 Fab 发射 kernel，确保碰撞 cell 集合只由
    // n 决定，与 DDF 的存储布局无关。
    MFItInfo collision_info;
    if (send_subset == 0) {
        collision_info.DisableDeviceSync().UseDefaultStream();
    }
    double finalize_start = 0.0;
    {
    MFIter mfi(state_lev, collision_info);
    for (; mfi.isValid(); ++mfi) {
        if (send_subset >= 0 &&
            osi_fillboundary_cache.at(lev).send_fabs.at(mfi.index()) != send_subset) {
            continue;
        }
        const Box ring = amrex::grow(mfi.validbox(), state_lev.nGrowVect());
        // 周期域外 ghost 可参与碰撞；非周期 ghost 由物理边界条件处理。
        const Box bx = amrex::grow(mfi.validbox(), n) & collision_domain;
        level_launch_cells += bx.numPts();
        const Array4<Real> state = state_lev.array(mfi);
        Array4<Real> s;
        Array4<Real> Ft;
        if (!use_osi && collide_mode == 0) {
            s = shear_lev.array(mfi);
            Ft = force_lev.array(mfi);
        }

        const Array4<const int> covered =
            has_fine_level ? covered_mask[lev].const_array(mfi)
                           : Array4<const int>{};
        const Array4<const int> interface =
            has_fine_level ? interface_mask[lev].const_array(mfi)
                           : Array4<const int>{};

        const auto collision_event = uniform_sample_active
            ? uniform_gpu_events.begin(UniformGpuEvents::Collision) : 0;
        // host 侧编译期分派使无 mask 闭包不捕获掩码参数；碰撞公式仍共用。
        if constexpr (!UseMask) {
            if (use_osi) {
                const auto [fab, phase_shift] = OSI::MakeOsiFabContext(ring, phase);
                amrex::ParallelFor(
                    bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                        collide_bgk_register_osi(
                            i, j, k, state, fab, phase_shift, omega_lev);
                    });
            } else if (collide_mode == 0) {
                amrex::ParallelFor(
                    bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                        collide(i, j, k, state, s, Ft, tau_lev, dt, hi);
                    });
            } else {
                amrex::ParallelFor(
                    bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                        collide_bgk_register(i, j, k, state, omega_lev);
                    });
            }
        } else {
            if (use_osi) {
                const auto [fab, phase_shift] = OSI::MakeOsiFabContext(ring, phase);
                amrex::ParallelFor(
                    bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                        if (has_fine_level && covered(i, j, k) != 0 &&
                            interface(i, j, k) == 0) {
                            return;
                        }
                        collide_bgk_register_osi(
                            i, j, k, state, fab, phase_shift, omega_lev);
                    });
            } else if (collide_mode == 0) {
                amrex::ParallelFor(
                    bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                        if (has_fine_level && covered(i, j, k) != 0 &&
                            interface(i, j, k) == 0) {
                            return;
                        }
                        collide(i, j, k, state, s, Ft, tau_lev, dt, hi);
                    });
            } else {
                amrex::ParallelFor(
                    bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                        if (has_fine_level && covered(i, j, k) != 0 &&
                            interface(i, j, k) == 0) {
                            return;
                        }
                        collide_bgk_register(i, j, k, state, omega_lev);
                    });
            }
        }
        if (uniform_sample_active) { uniform_gpu_events.end(collision_event); }
    }
    // MFIter析构保持原有多stream等待；这里只包围它计时，不另加等待。
    if (uniform_sample_active) { finalize_start = amrex::second(); }
    }
    if (uniform_sample_active) {
        perf_stats.uniform_sample.cpu_collide_finalize += amrex::second() - finalize_start;
    }
    // 延后子集留在 stream 0，由通信后的同 stream 消费和阶段同步保证完成。
    // 普通子集已有 MFIter 的多 stream 等待；AMR/诊断保留原显式等待。
    if (send_subset != 0 && !useUniformReducedSync()) {
        amrex::Gpu::streamSynchronize();
    }
    // 重叠子集这里只累计提交时间，其 GPU 执行在通信区间中完成；
    // collide/comm 不再表示独立 GPU 内核成本，端到端比较使用 total。
    const double collide_elapsed = amrex::second() - collide_start;
    perf_stats.collide += collide_elapsed;
    perf_stats.collide_level.at(lev) += collide_elapsed;
    // 两个互斥子集共同计为一次推进，发射 cell 数分别累加。
    if (send_subset != 1) {
        ++perf_stats.collide_level_calls.at(lev);
    }
    perf_stats.collide_level_launch_cells.at(lev) += level_launch_cells;
}

void AmrCoreLBM::Stream(int lev, int n, DdfLayout layout) {
    if (layout == DdfLayout::Osi) {
        // OSI 的逻辑迁移由 phase 提交完成，不搬运体积级 DDF。
        ScopedPerfTimer timer(perf_stats.stream, !useUniformReducedSync());
        ++osi_phase.at(lev);
        // 当前 stream 上更新，后续边界、通信和粗细层传输读取同一份偏移。
        osi_fab_phase_cache.at(lev).update(osi_phase.at(lev));
        return;
    }

    ScopedPerfTimer timer(perf_stats.stream, !useUniformReducedSync());
    AMREX_ALWAYS_ASSERT(n >= 1);
    AMREX_ALWAYS_ASSERT(n - 1 <= 1);

    amrex::MultiFab& f_old_lev = f_old[lev];
    amrex::MultiFab& f_new_lev = f_new[lev];
    const bool has_fine_level = (lev < finest_level);

    for (MFIter mfi(f_old_lev, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        // The outer ghost layer supplies pull-streaming data for the inner layer.
        const auto bx = mfi.growntilebox(n - 1);
        const Array4<Real>& fold = f_old_lev.array(mfi);
        const Array4<Real>& fnew = f_new_lev.array(mfi);

        if (has_fine_level) {
            const Array4<const int>& covered = covered_mask[lev].const_array(mfi);

            amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                if (covered(i, j, k) != 0) {
                    return;
                }

                stream(i, j, k, fold, fnew);
            });
        } else {
            amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                stream(i, j, k, fold, fnew);
            });
        }
    }
}

void AmrCoreLBM::SwapLevel(int lev, int n, DdfLayout layout) {
    if (layout == DdfLayout::Osi) {
        // OSI 只有一份体积级状态，Stream 阶段提交 phase 后无需交换数组。
        return;
    }

    ScopedPerfTimer timer(perf_stats.swap, !useUniformReducedSync());
    amrex::MultiFab& f_old_lev = f_old[lev];
    amrex::MultiFab& f_new_lev = f_new[lev];

    std::swap(f_old_lev, f_new_lev);
}
