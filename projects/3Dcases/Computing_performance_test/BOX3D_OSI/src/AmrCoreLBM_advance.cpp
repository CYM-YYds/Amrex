#include "AmrCoreLBM.H"
#include "AmrCoreLBM_detail.H"

#include <AMReX_MultiFabUtil.H>
#include <AMReX_Utility.H>
#include "Kernels.H"

using namespace amrex;
using namespace Box3dDetail;

// 生产推进阶段与数据路径分派。

void AmrCoreLBM::ComputeMacroLevel(int lev) {
    if (stream_mode == 1) {
        const amrex::MultiFab& state = osi_state[lev];
        amrex::MultiFab& rho_lev = density[lev];
        amrex::MultiFab& u_lev = velocity[lev];
        const std::uint64_t phase = osi_phase[lev];

        for (MFIter mfi(state, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
            const Box& bx = mfi.tilebox();
            const Box ring_box = amrex::grow(mfi.validbox(), state.nGrowVect());
            const auto fab_lo = ring_box.smallEnd();
            const OSI::FabGeometry fab{
                {fab_lo[0], fab_lo[1], fab_lo[2]},
                {ring_box.length(0), ring_box.length(1), ring_box.length(2)}};
            const auto phase_shift = OSI::osi_phase_shift(phase, fab);
            const Array4<const Real>& ddf = state.const_array(mfi);
            const Array4<Real>& rho = rho_lev.array(mfi);
            const Array4<Real>& u = u_lev.array(mfi);

            amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                compute_macro_osi(i, j, k, ddf, rho, u, fab, phase_shift);
            });
        }
        return;
    }

    amrex::MultiFab& ddf = f_old[lev];
    amrex::MultiFab& rho_lev = density[lev];
    amrex::MultiFab& u_lev = velocity[lev];

    for (MFIter mfi(ddf, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const Box& bx = mfi.growntilebox(nghost);
        const Array4<Real>& fold = ddf.array(mfi);
        const Array4<Real>& rho = rho_lev.array(mfi);
        const Array4<Real>& u = u_lev.array(mfi);

        amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
            compute_macro(i, j, k, fold, rho, u);
        });
    }
}

void AmrCoreLBM::ComputeMacro() {
    AverageDownValid();
    for (int lev = 0; lev <= finest_level; lev++) {
        ComputeMacroLevel(lev);
    }
}

void AmrCoreLBM::ComputeVorticityLevel(int lev) {

    const amrex::MultiFab& level_layout =
        stream_mode == 1 ? osi_state.at(lev) : f_old.at(lev);
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
        stream_mode == 1 ? osi_state.at(lev) : f_old.at(lev);
    amrex::MultiFab& u_lev = velocity[lev];
    amrex::MultiFab& shear_lev = shear[lev];
    amrex::Real dt = Geom(lev).CellSizeArray()[0];

    for (MFIter mfi(level_layout, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const Box& bx = mfi.growntilebox(0);

        Array4<Real> const& u = u_lev.array(mfi);
        Array4<Real> const& shear = shear_lev.array(mfi);

        amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
            compute_shear(i, j, k, u, shear, dt);
        });
    }
}

void AmrCoreLBM::ComputeShear() {
    for (int lev = 0; lev <= finest_level; lev++) {
        ComputeShearLevel(lev);
    }
}

void AmrCoreLBM::AverageDownValidLevel(int lev, bool is_scale) {
    // amrex::average_down(f_old[lev+1], f_old[lev], geom[lev+1], geom[lev],0, Q, refRatio(lev));

    amrex::MultiFab& fine_mf = f_old[lev + 1];
    amrex::MultiFab& crse_mf = f_old[lev];

    amrex::MultiFab& fine_boundary_data = f_new.at(lev + 1);

    amrex::MultiFab::Copy(fine_boundary_data, fine_mf, 0, 0, Q, 0);

    if (is_scale) {
        amrex::Real scale = 2.0 * tau[lev] / tau[lev + 1];

        for (MFIter mfi(fine_boundary_data, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
            const auto bx = mfi.growntilebox(0);

            const Array4<Real>& fold = fine_boundary_data.array(mfi);

            amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                average_scale(i, j, k, fold, scale);
            });
        }
    }
    amrex::average_down(fine_boundary_data, crse_mf, 0, Q, refRatio(lev));
}

void AmrCoreLBM::AverageDownValid(const std::function<void()>& before_repair) {
    // interface-only 时间推进不会更新深层 covered 粗单元；regrid 可能重新暴露这些单元，
    // 因此重网格前必须先将全部细层 valid 数据完整同步到粗层父单元。
    for (int lev = finest_level - 1; lev >= 0; --lev) {
        if (stream_mode == 1) {
            AverageDownOsiValidLevel(lev, true);
            if (osiReferenceEnabled()) {
                // 锁步参考态也要在重网格前完成全部有效单元的平均下传。
                AverageDownValidLevel(lev, true);
            }
        } else {
            AverageDownValidLevel(lev, true);
        }
    }
    // 保留平均后、边界修复前的诊断阶段；单层网格也执行边界修复。
    if (finest_level > 0 && before_repair) {
        before_repair();
    }
    RepairCurrentStatePhysicalBoundary();
}

void AmrCoreLBM::RepairCurrentStatePhysicalBoundary() { // 在平均后，修复当前状态的物理边界条件。
    ScopedPerfTimer timer(perf_stats.boundary);
    const DdfLayout layout =
        stream_mode == 1 ? DdfLayout::Osi : DdfLayout::Canonical;

    // 完整平均下传或重网格插值可能覆盖物理边界 valid 单元；此处必须包含
    // covered 单元，以便这些数据在后续重新暴露或作为插值源时仍满足边界条件。
    for (int lev = 0; lev <= finest_level; ++lev) {
        amrex::MultiFab& state =
            layout == DdfLayout::Osi ? osi_state.at(lev) : f_old.at(lev);
        ApplyPhysicalBoundaryLevel(lev, state, layout, false);
        if (osiReferenceEnabled() && layout == DdfLayout::Osi) {
            // oracle 必须经历与 OSI 当前态相同的初始化/平均后边界修复。
            ApplyPhysicalBoundaryLevel(
                lev, f_old.at(lev), DdfLayout::Canonical, false);
        }
    }
}

void AmrCoreLBM::AverageDownInterfaceLevel(int fine_lev, bool is_scale) {
    const int coarse_lev = fine_lev - 1;
    AMREX_ALWAYS_ASSERT(fine_lev > 0 && fine_lev <= finest_level);
    ScopedPerfTimer timer(perf_stats.average);
    ++perf_stats.avgdown_calls;

    static const bool average_probe_enabled = [] {
        bool value = false;
        ParmParse("verification").query("average_probe_first", value);
        return value;
    }();
    static int level0_average_count = 0;
    const bool probe_this_call = average_probe_enabled && coarse_lev == 0 && is_scale &&
                                 ++level0_average_count == 1;
    if (probe_this_call) {
        ProbeAverageInterfaceLevel(coarse_lev, "before"); // 导出一次界面平均前或后的状态快照，供离线逐单元比较
    }

    if (stream_mode == 1) {
        AverageDownOsiLevel(fine_lev, is_scale);
        if (!osiReferenceEnabled()) {
            if (probe_this_call) {
                ProbeAverageInterfaceLevel(coarse_lev, "after");
            }
            return;
        }
        // 锁步模式继续为 A-B 参考态执行相同的界面平均。
    }

    amrex::MultiFab& fine_mf = f_old[fine_lev];
    amrex::MultiFab& crse_mf = f_old[coarse_lev];

    const IntVect ratio = refRatio(coarse_lev);
    // 平均前先将细层非平衡分布函数转换到粗层松弛时间对应的尺度。
    const Real scale = 2.0 * tau[coarse_lev] / tau[fine_lev];
    ScopedPerfTimer avgdown_timer(perf_stats.average_down);

    {
        const Long children_per_parent = ratio[0] * ratio[1] * ratio[2];
        // 交界区域融合路径只处理缓存的 coarse-interface 父单元。
        MultiFab& interface_result = average_interface_buffer[coarse_lev];
        const Vector<int>& fine_box_indices = average_interface_fine_box[coarse_lev];
        if (fine_box_indices.empty()) {
            return;
        }

        AMREX_ALWAYS_ASSERT(interface_result.size() == fine_box_indices.size());

        {
            // 缩放与插值融合，不写回完整的缩放后 DDF 中间数据。
            ScopedPerfTimer fused_timer(perf_stats.average_fused);
            for (MFIter mfi(interface_result, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
                const int fine_index = fine_box_indices[mfi.index()];
                const Box bx = mfi.tilebox();
                const Array4<const Real>& fine = fine_mf.const_array(fine_index);
                const Array4<Real>& coarse = interface_result.array(mfi);
                perf_stats.average_parent_cells += bx.numPts();
                if (is_scale) {
                    perf_stats.average_scale_cells += bx.numPts() * children_per_parent;
#ifdef AMREX_USE_CUDA
                    // 一个 warp 负责一个粗层父单元，D3Q27 分量分配给不同 lane；相比
                    // 单线程负责整个父单元，可显著降低单线程寄存器占用。
                    constexpr int threads_per_block = 256;
                    constexpr int warp_size = 32;
                    constexpr int warps_per_block = threads_per_block / warp_size;
                    const Long ncells = bx.numPts();
                    const int nblocks = static_cast<int>((ncells + warps_per_block - 1) /
                                                         warps_per_block);
                    const auto lo = amrex::lbound(bx); // 返回各方向最小索引
                    const int nx = bx.length(0);       // x方向格点数量
                    const int ny = bx.length(1);       // y方向格点数量
                    amrex::launch<threads_per_block>(
                        nblocks, amrex::Gpu::Device::gpuStream(),
                        [=] AMREX_GPU_DEVICE() noexcept {
                            const int lane = threadIdx.x % warp_size;          // threadIdx.x 是线程在当前 block 内的编号, lane 是线程在 warp 内的编号
                            const int warp_in_block = threadIdx.x / warp_size; // 取得 block 内 warp 编号
                            const Long icell = static_cast<Long>(blockIdx.x) * warps_per_block + warp_in_block;
                            if (icell < ncells) { // 排除了最后一个 block 中的多余 warp
                                // GPU内部的线程编号映射到AmreX中的网格坐标
                                const int i = lo.x + static_cast<int>(icell % nx);
                                const Long yz = icell / nx;
                                const int j = lo.y + static_cast<int>(yz % ny);
                                const int k = lo.z + static_cast<int>(yz / ny);
                                average_down_lbm_scaled_warp(i, j, k, lane, coarse, fine,
                                                             ratio, scale);
                            }
                        });
#else
                    amrex::ParallelFor(
                        bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {
                            average_down_lbm_scaled(i, j, k, coarse, fine, ratio, scale);
                        });
#endif
                } else {
                    amrex::ParallelFor(
                        bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {
                            average_down_lbm(i, j, k, coarse, fine, ratio);
                        });
                }
            }
        }

        {
            ScopedPerfTimer copyback_timer(perf_stats.average_copyback);
            // 稀疏缓冲区沿用细层数据归属；ParallelCopy 负责将结果通过本地复制或
            // 必要的 MPI 通信写入真实粗层布局。
            crse_mf.ParallelCopy(interface_result, 0, 0, Q);
        }
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

    if (stream_mode == 1) {
        FillOsiGhostFromCoarse(coarse_lev, time, is_scale);
        if (osiReferenceEnabled()) {
            // 锁步参考态必须与独立 A-B 一样填充粗到细 ghost。
            FillDdfGhostFromCoarse(coarse_lev, time, is_scale);
        }
    } else {
        FillDdfGhostFromCoarse(coarse_lev, time, is_scale);
    }
    static const bool probe_first_fill = [] {
        bool enabled = false;
        ParmParse("verification").query("ghost_probe_first_fill", enabled);
        return enabled;
    }();
    if (!probe_first_fill || fine_lev != 1) {
        return;
    }
    static int level1_fill_count = 0;
    if (++level1_fill_count == 1) {
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
    ScopedPerfTimer timer(perf_stats.comm);
    if (layout == DdfLayout::Osi) {
        FillBoundaryOsi(lev);
        return;
    }

    amrex::MultiFab& f_old_lev = f_old[lev];
    f_old_lev.FillBoundary(geom[lev].periodicity());
}

void AmrCoreLBM::FillBoundaryOsi(int lev) {
    // local-direct 负责同一 rank 的 grown-Fab seam；跨 rank 的自定义
    // pack/unpack 只有在 mpi-direct 打开时才允许进入。
    if (osi_local_direct &&
        (ParallelDescriptor::NProcs() == 1 || osi_mpi_direct)) {
        CommunicateOsiLevelLocalDirect(lev);
    } else {
        CommunicateOsiLevel(lev);
    }
}

void AmrCoreLBM::AdvanceOneLayout(int lev, DdfLayout layout) {
    // 生产推进只执行一次布局；锁步检测由 AdvanceLevel 显式编排两次布局。
    Collide(lev, nghost, layout);
    CommunicateLevel(lev, layout);
    Stream(lev, nghost, layout);
    Boundary(lev, layout);
}

void AmrCoreLBM::AdvanceLevel(int lev) {
    static int coarse_advance_count = 0;
    if (lev == 0) {
        ++coarse_advance_count;
    }
    const DdfLayout layout =
        stream_mode == 1 ? DdfLayout::Osi : DdfLayout::Canonical;
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
        Collide(lev, nghost, DdfLayout::Osi);
        Collide(lev, nghost, DdfLayout::Canonical);
        CompareOsiReferenceStage(lev, "Collision", f_old.at(lev), 0);

        CommunicateLevel(lev, DdfLayout::Osi);
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
        CompareOsiReferenceStage(lev, "Boundary", f_new.at(lev), 0);
        // 边界处理后再次确认该层所有 uncovered valid 单元。
        CompareOsiReferenceStage(lev, "BoundaryUncovered", f_new.at(lev), 0);

        SwapLevel(lev, nghost, DdfLayout::Osi);
        SwapLevel(lev, nghost, DdfLayout::Canonical);
        CompareOsiReferenceStage(lev, "Swap", f_old.at(lev), 0);
        return;
    }

    // 生产模式只经过一次布局推进；锁步分支已在上方显式执行两种布局。
    AdvanceOneLayout(lev, layout);
    int boundary_probe_step = -1;
    ParmParse("verification").query("boundary_probe_step", boundary_probe_step);
    if (lev == 0 && coarse_advance_count == boundary_probe_step) {
        // 在第 33 步已知的 uncovered 格点读取 Boundary 返回后的当前 DDF。
        const IntVect sample(AMREX_D_DECL(1, 111, 111));
        constexpr int sample_q = 18;
        const MultiFab& state = layout == DdfLayout::Osi
                                    ? osi_state.at(0)
                                    : f_new.at(0);
        for (MFIter mfi(state, false); mfi.isValid(); ++mfi) {
            if (!mfi.validbox().contains(sample)) {
                continue;
            }
            const FArrayBox& device = state[mfi];
            FArrayBox host(device.box(), Q, The_Pinned_Arena());
            Gpu::dtoh_memcpy(host.dataPtr(), device.dataPtr(), host.nBytes());
            IntVect address = sample;
            if (layout == DdfLayout::Osi) {
                const Box ring = device.box();
                const auto lo = ring.smallEnd();
                const OSI::FabGeometry fab{
                    {lo[0], lo[1], lo[2]},
                    {ring.length(0), ring.length(1), ring.length(2)}};
                const auto shift = OSI::osi_phase_shift(osi_phase.at(0), fab);
                const auto raw = OSI::osi_address(
                    {sample[0], sample[1], sample[2]}, {1, 0, -1}, fab, shift);
                address = IntVect(AMREX_D_DECL(raw.x, raw.y, raw.z));
            }
            IArrayBox host_mask(covered_mask.at(0)[mfi].box(), 1,
                                The_Pinned_Arena());
            const auto& device_mask = covered_mask.at(0)[mfi];
            Gpu::dtoh_memcpy(host_mask.dataPtr(), device_mask.dataPtr(),
                             host_mask.nBytes());
            auto output = amrex::AllPrint();
            output.SetPrecision(17);
            output << "boundary_probe: step=" << coarse_advance_count
                   << " mode=" << stream_mode << " lev=0 logical=" << sample
                   << " q=" << sample_q
                   << " covered=" << host_mask.const_array()(sample[0], sample[1], sample[2])
                   << " value=" << host.const_array()(address[0], address[1], address[2], sample_q) << '\n';
        }
    }
    SwapLevel(lev, nghost, layout);
}

void AmrCoreLBM::ApplyPhysicalBoundaryLevel(
    int lev, amrex::MultiFab& state_lev, DdfLayout layout,
    bool skip_covered) {
    const bool use_osi = layout == DdfLayout::Osi;
    const Box domain = Geom(lev).Domain();
    const amrex::IntVect hi{domain.length(0) - 1,
                            domain.length(1) - 1,
                            domain.length(2) - 1};
    const auto is_periodic = Geom(lev).isPeriodicArray();
    const bool has_fine_level =
        skip_covered && lev < finest_level && cf_mask_mode == 1;
    const std::uint64_t phase = use_osi ? osi_phase.at(lev) : 0;

    for (MFIter mfi(state_lev, false); mfi.isValid(); ++mfi) {
        const Box ring = amrex::grow(mfi.validbox(), state_lev.nGrowVect());
        const auto lo = ring.smallEnd();
        const OSI::FabGeometry fab{
            {lo[0], lo[1], lo[2]},
            {ring.length(0), ring.length(1), ring.length(2)}};
        const auto phase_shift =
            OSI::osi_phase_shift(phase, fab);
        const Array4<Real> state = state_lev.array(mfi);
        const Array4<const int> covered =
            has_fine_level ? covered_mask[lev].const_array(mfi)
                           : Array4<const int>{};
        perf_stats.boundary_full_cells += mfi.tilebox().numPts();

        for (const Box& bx : boundary_work_boxes[lev][mfi.index()]) { // 用 mfi.index() 得到该 Box 的全局编号
            perf_stats.boundary_launch_cells += bx.numPts();
            if (use_osi) {
                amrex::ParallelFor(
                    bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                        if (has_fine_level && covered(i, j, k) != 0) {
                            return;
                        }
                        fill_boundary_osi_state(
                            i, j, k, state, hi, is_periodic, fab,
                            phase_shift);
                    });
            } else {
                amrex::ParallelFor(
                    bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                        if (has_fine_level && covered(i, j, k) != 0) {
                            return;
                        }
                        fill_boundary(i, j, k, state, hi, is_periodic);
                    });
            }
        }
    }
}

void AmrCoreLBM::Boundary(int lev, DdfLayout layout) {
    ScopedPerfTimer timer(perf_stats.boundary);
    amrex::MultiFab& state =
        layout == DdfLayout::Osi ? osi_state.at(lev) : f_new.at(lev);
    ApplyPhysicalBoundaryLevel(lev, state, layout, true);
}

void AmrCoreLBM::Collide(int lev, int n, DdfLayout layout) {
    const double collide_start = amrex::second();

    const bool use_osi = layout == DdfLayout::Osi;
    const Box domain = Geom(lev).Domain();
    const amrex::IntVect hi{domain.length(0) - 1,
                            domain.length(1) - 1,
                            domain.length(2) - 1};
    amrex::MultiFab& state_lev =
        use_osi ? osi_state.at(lev) : f_old.at(lev);
    amrex::MultiFab& shear_lev = shear[lev];
    amrex::MultiFab& force_lev = force[lev];
    amrex::Real dt = Geom(lev).CellSizeArray()[0];
    amrex::Real tau_lev = tau[lev];
    const amrex::Real omega_lev = 1.0 / tau_lev;
    const Box collision_domain = Geom(lev).growPeriodicDomain(n);
    const bool has_fine_level = lev < finest_level && cf_mask_mode == 1;
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
    for (MFIter mfi(state_lev, false); mfi.isValid(); ++mfi) {
        const Box ring = amrex::grow(mfi.validbox(), state_lev.nGrowVect());
        // 周期域外 ghost 可参与碰撞；非周期 ghost 由物理边界条件处理。
        const Box bx = amrex::grow(mfi.validbox(), n) & collision_domain;
        level_launch_cells += bx.numPts();
        const auto lo = ring.smallEnd();
        const OSI::FabGeometry fab{
            {lo[0], lo[1], lo[2]},
            {ring.length(0), ring.length(1), ring.length(2)}};
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

        // 模式判断停留在 host 启动层，避免把运行时分支和另一种存储路径的
        // 寄存器需求带入同一个 GPU kernel。
        if (use_osi) {
            const auto phase_shift = OSI::osi_phase_shift(phase, fab);
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
    amrex::Gpu::streamSynchronize();
    const double collide_elapsed = amrex::second() - collide_start;
    perf_stats.collide += collide_elapsed;
    perf_stats.collide_level.at(lev) += collide_elapsed;
    ++perf_stats.collide_level_calls.at(lev);
    perf_stats.collide_level_launch_cells.at(lev) += level_launch_cells;
}

void AmrCoreLBM::Stream(int lev, int n, DdfLayout layout) {
    if (layout == DdfLayout::Osi) {
        // OSI 的逻辑迁移由 phase 提交完成，不搬运体积级 DDF。
        ++osi_phase.at(lev);
        return;
    }

    ScopedPerfTimer timer(perf_stats.stream);
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

        if (has_fine_level && cf_mask_mode == 1) {
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

    ScopedPerfTimer timer(perf_stats.swap);
    amrex::MultiFab& f_old_lev = f_old[lev];
    amrex::MultiFab& f_new_lev = f_new[lev];

    std::swap(f_old_lev, f_new_lev);
}

void AmrCoreLBM::InterpScale(int lev, int n) // n=nghost
{
    amrex::AllPrint() << "InterpScale not existing !" << std::endl;
}

void AmrCoreLBM::AverageScale(int lev, int n) {
    amrex::AllPrint() << "AverageScale not existing !" << std::endl;
}
