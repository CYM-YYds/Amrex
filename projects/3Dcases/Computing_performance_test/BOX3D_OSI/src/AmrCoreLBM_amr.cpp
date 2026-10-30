#include "AmrCoreLBM.H"

#include <AMReX_BoxList.H>
#include <AMReX_MultiFabUtil.H>
#include <AMReX_ParIter.H>
#include <AMReX_PhysBCFunct.H>
#include <AMReX_Utility.H>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include "Kernels.H"

using namespace amrex;
using namespace Box3dDetail;

// 网格生命周期与通用粗细层传输。

namespace {
amrex::BoxList MissingInterpolationSources(
    const amrex::Box& coarse_stencil, const amrex::BoxArray& coarse_valid,
    const amrex::Geometry& coarse_geom) {
    amrex::Box required = coarse_stencil;
    const auto& domain = coarse_geom.Domain();
    for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
        if (!coarse_geom.isPeriodic(dir)) {
            // 与物理边界延拓使用相同的坐标钳制，检查域外模板的域内来源。
            required.setSmall(
                dir, std::clamp(required.smallEnd(dir),
                                domain.smallEnd(dir), domain.bigEnd(dir)));
            required.setBig(
                dir, std::clamp(required.bigEnd(dir),
                                domain.smallEnd(dir), domain.bigEnd(dir)));
        }
    }
    // 同层 valid 并集及其周期像都属于合法来源；父层 ghost 不计入覆盖。
    return coarse_valid.complementIn(required, coarse_geom.periodicity());
}

void RequireInterpolationCoverage(
    const amrex::Box& coarse_stencil, const amrex::BoxArray& coarse_valid,
    const amrex::Geometry& coarse_geom, int fine_lev, int fine_index,
    const amrex::Box& fine_work_box, int interp_mode,
    const amrex::IntVect& fine_nghost, int n_proper) {
    const auto missing =
        MissingInterpolationSources(coarse_stencil, coarse_valid, coarse_geom);
    if (missing.isEmpty()) {
        return;
    }

    std::ostringstream message;
    message << "[InterpolationCoverage] coarse valid coverage is insufficient"
            << "\n粗到细插值模板存在域内缺口，已停止计算，避免读取未填充的 staging 数据。"
            << "\ncoarse_level=" << fine_lev - 1
            << " fine_level=" << fine_lev << " fine_fab=" << fine_index
            << " interp_mode=" << interp_mode
            << " fine_nghost=" << fine_nghost << " amr.n_proper=" << n_proper
            << "\nfine_work_box=" << fine_work_box
            << "\ncoarse_stencil=" << coarse_stencil
            << "\ncoarse_domain=" << coarse_geom.Domain()
            << "\nmissing_coarse_boxes=";
    for (const auto& box : missing) {
        message << ' ' << box;
    }
    message << "\n请考虑增大 amr.n_proper（proper nesting），并核对 blocking_factor、"
               "ref_ratio 和插值模板宽度，重新生成满足嵌套要求的网格；"
               "或接入多层递归补缺功能（amrex::FillPatchNLevels），"
               "并适配 DDF 缩放及 OSI 数据布局。"
            << "\n当前插值未接入多层递归补缺，也不会自动读取 coarse ghost。"
            << "\n非周期域外模板按物理边界延拓检查其域内来源；周期像已纳入覆盖检查。";
    amrex::Abort(message.str());
}

template <class T>
amrex::Gpu::DeviceVector<T>
convertToDeviceVector(amrex::Vector<T> v) {
    int ncomp = v.size();
    amrex::Gpu::DeviceVector<T> v_d(ncomp);
#ifdef AMREX_USE_GPU
    amrex::Gpu::htod_memcpy(v_d.data(), v.data(), sizeof(T) * ncomp);
#else
    std::memcpy(v_d.data(), v.data(), sizeof(T) * ncomp);
#endif
    return v_d;
}

} // namespace

void AmrCoreLBM::InitMesh(amrex::Real cur_time) {
    InitFromScratch(cur_time);
    RebuildCoarseFineCaches();
}

// 只复用 AMReX 插值器的模板几何，不调用其数值插值函数。
amrex::Interpolater* AmrCoreLBM::DdfInterpolater() const {
    if (interp_mode == 1) {
        return &cell_cons_interp;
    }
    if (interp_mode == 2) {
        return &quadratic_interp;
    }
    return &cell_bilinear_interp;
}

void AmrCoreLBM::BuildInterpolationStage(
    int lev, const MultiFab& coarse, const MultiFab& fine,
    const Vector<InterpolationPatchTag>& patches, MultiFab& stage) const {
    AMREX_ALWAYS_ASSERT(lev > 0 && refRatio(lev - 1) == IntVect(2));
    const auto coarsener = DdfInterpolater()->BoxCoarsener(refRatio(lev - 1));
    const auto coarse_valid = coarse.boxArray().simplified();
    Vector<Box> coarse_boxes;
    Vector<int> owners;
    stage.clear();
    for (const auto& patch : patches) {
        const auto& work = patch.fine_box;
        const int fine_index = patch.fine_box_index;
        const Box stencil = coarsener.doit(work);
        RequireInterpolationCoverage(
            stencil, coarse_valid, Geom(lev - 1), lev, fine_index,
            work, interp_mode,
            fine.boxArray()[fine_index].contains(work)
                ? IntVect(0)
                : fine.nGrowVect(),
            nProper());
        // 每个工作区保留独立模板，owner 与目标 fine Fab 相同。
        // 模板可重叠，但不会把包围它们的无关粗层区域一并分配。
        coarse_boxes.push_back(stencil);
        owners.push_back(fine.DistributionMap()[fine_index]);
    }
    if (!coarse_boxes.empty()) {
        stage.define(BoxArray(coarse_boxes.data(), coarse_boxes.size()),
                     DistributionMapping(std::move(owners)), Q, 0);
    }
}

void AmrCoreLBM::ApplyPhysicalBoundaryToInterpolationStage(
    int lev, MultiFab& coarse_stage) {
    AMREX_ALWAYS_ASSERT(lev >= 0 && lev < max_level);
    const Box coarse_domain = Geom(lev).Domain();
    const auto coarse_lo = amrex::lbound(coarse_domain);
    const auto coarse_hi = amrex::ubound(coarse_domain);
    const auto coarse_periodic = Geom(lev).isPeriodicArray();

    // coarse_stage 的域外 stencil 沿最近的非周期域内单元延拓；周期方向
    // 已由调用者的 ParallelCopy(periodicity) 完成映射。
    for (MFIter mfi(coarse_stage, false); mfi.isValid(); ++mfi) {
        const Box stage_box = mfi.validbox();
        // 是否触及物理域外由现有模板几何直接推导，不保存派生标志。
        bool needs_fill = false;
        for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
            needs_fill = needs_fill ||
                         (!coarse_periodic[dir] &&
                          (stage_box.smallEnd(dir) < coarse_domain.smallEnd(dir) ||
                           stage_box.bigEnd(dir) > coarse_domain.bigEnd(dir)));
        }
        if (!needs_fill) {
            continue;
        }
        const auto coarse = coarse_stage.array(mfi);
        amrex::ParallelFor(
            stage_box, Q,
            [=] AMREX_GPU_DEVICE(int i, int j, int k, int q) {
                const int src_i = coarse_periodic[0]
                                      ? i
                                      : amrex::max(coarse_lo.x,
                                                   amrex::min(i, coarse_hi.x));
                const int src_j = coarse_periodic[1]
                                      ? j
                                      : amrex::max(coarse_lo.y,
                                                   amrex::min(j, coarse_hi.y));
                const int src_k = AMREX_D_PICK(0, 0, coarse_periodic[2])
                                      ? k
                                      : amrex::max(coarse_lo.z,
                                                   amrex::min(k, coarse_hi.z));
                if (src_i != i || src_j != j || src_k != k) {
                    coarse(i, j, k, q) = coarse(src_i, src_j, src_k, q);
                }
            });
    }
}

template <bool UseOsiAddress>
void AmrCoreLBM::InterpolateDdfStageBatch(
    MultiFab& coarse_stage, MultiFab& fine_state,
    const InterpolationBatchCache& cache, const OSI::PhaseShift3* shifts,
    bool apply_scale, Real scale) {
    // 所有 stage 先缩放，再在同一 stream 上批量插值，保持读写依赖。
    if (apply_scale) {
        amrex::ParallelFor(cache.scale_tags,
                           [=] AMREX_GPU_DEVICE(int i, int j, int k, const amrex::Array4BoxTag<Real>& tag) {
                               average_scale(i, j, k, tag.dfab, scale);
                           });
    }
    // 每次调用取当前数组，避免 A-B 双缓冲交换后仍访问旧存储。
    const auto stages = coarse_stage.const_arrays();
    const auto fines = fine_state.arrays();
    if (interp_mode == 0) {
        amrex::ParallelFor(cache.tags,
                           [=] AMREX_GPU_DEVICE(int i, int j, int k, const InterpolationBatchTag& tag) {
                               interp_bilinear_ddf_impl<UseOsiAddress>(
                                   i, j, k, fines[tag.fine_local_index], stages[tag.stage_local_index],
                                   tag.fab, UseOsiAddress ? shifts[tag.phase_index] : OSI::PhaseShift3{});
                           });
    } else if (interp_mode == 1) {
        amrex::ParallelFor(cache.tags,
                           [=] AMREX_GPU_DEVICE(int i, int j, int k, const InterpolationBatchTag& tag) {
                               interp_cell_cons_linear_children_ddf_impl<UseOsiAddress>(
                                   i, j, k, fines[tag.fine_local_index], stages[tag.stage_local_index],
                                   tag.target, tag.fab, UseOsiAddress ? shifts[tag.phase_index] : OSI::PhaseShift3{});
                           });
    } else {
        amrex::ParallelFor(cache.tags,
                           [=] AMREX_GPU_DEVICE(int i, int j, int k, const InterpolationBatchTag& tag) {
                               interp_cell_quadratic_children_ddf_impl<UseOsiAddress>(
                                   i, j, k, fines[tag.fine_local_index], stages[tag.stage_local_index],
                                   tag.target, tag.fab, UseOsiAddress ? shifts[tag.phase_index] : OSI::PhaseShift3{});
                           });
    }
}

void AmrCoreLBM::FillDdfGhostFromCoarse(
    int lev, amrex::Real time, bool apply_scale, DdfLayout layout) {
    (void)time;
    const int fine_lev = lev + 1;
    AMREX_ALWAYS_ASSERT(lev >= 0 && fine_lev <= finest_level);
    AMREX_ALWAYS_ASSERT(refRatio(lev) == IntVect(2));
    const bool use_osi = layout == DdfLayout::Osi;
    auto& fine_state = use_osi ? osi_state.at(fine_lev) : f_old.at(fine_lev);
    auto& cache = interpolation_cache.at(fine_lev);
    auto& coarse_stage = cache.coarse_stage;
    const Real scale = tau.at(fine_lev) / tau.at(lev) / Real(2.0);
    if (cache.patches.empty()) {
        return;
    }
    AMREX_ALWAYS_ASSERT(cache.work.tags.is_defined() && cache.work.scale_tags.is_defined());
    AMREX_ALWAYS_ASSERT(cache.work.tags.ntags == coarse_stage.local_size() &&
                        cache.work.scale_tags.ntags == coarse_stage.local_size());
    ScopedPerfTimer timer(perf_stats.interp_fillpatch,
                          !use_osi || params_.perf_osi_detail_timing);
    // 无本地模板的 rank 仍须参与通信；两种布局都先装入 canonical stage。
    if (use_osi) {
        AMREX_ALWAYS_ASSERT(cache.copy.ready);
        ParallelCopyOsi(cache.copy, osi_fab_phase_cache.at(lev).data(osi_phase.at(lev)));
    } else {
        coarse_stage.ParallelCopy(f_old.at(lev), 0, 0, Q, IntVect(0), IntVect(0),
                                  Geom(lev).periodicity());
    }
    ApplyPhysicalBoundaryToInterpolationStage(lev, coarse_stage);
    if (use_osi) {
        InterpolateDdfStageBatch<true>(coarse_stage, fine_state, cache.work,
                                       osi_fab_phase_cache.at(fine_lev).data(osi_phase.at(fine_lev)), apply_scale, scale);
    } else {
        InterpolateDdfStageBatch<false>(coarse_stage, fine_state, cache.work,
                                        nullptr, apply_scale, scale);
    }
}

void AmrCoreLBM::BuildInterpolationWork(
    MultiFab& coarse_stage, const MultiFab& fine,
    const Vector<InterpolationPatchTag>& patches, InterpolationBatchCache& work,
    const OSI::FabPhaseCache* phases) const {
    work.clear();
    Vector<amrex::Array4BoxTag<Real>> scale_tags;
    Vector<InterpolationBatchTag> tags;
    for (MFIter mfi(coarse_stage, false); mfi.isValid(); ++mfi) {
        const auto& patch = patches.at(mfi.index());
        const int fi = patch.fine_box_index;
        const auto fab = OSI::MakeOsiFabContext(
                             amrex::grow(fine.boxArray()[fi], fine.nGrowVect()), 0)
                             .fab;
        const int local_fine = fine.localindex(fi);
        AMREX_ALWAYS_ASSERT(local_fine >= 0);
        scale_tags.push_back({coarse_stage.array(mfi), mfi.validbox()});
        tags.push_back({interp_mode == 0 ? patch.fine_box : amrex::coarsen(patch.fine_box, 2),
                        patch.fine_box, fab, mfi.LocalIndex(), local_fine,
                        phases ? phases->slot(fi) : -1});
    }
    work.scale_tags.define(scale_tags);
    work.tags.define(tags);
}

void AmrCoreLBM::BuildRestrictionWork(
    const MultiFab& fine, const MultiFab& result, const Vector<int>& fine_indices,
    Gpu::DeviceVector<RestrictionBlockTag>& work, const OSI::FabPhaseCache* phases) const {
    work.clear();
#ifdef AMREX_USE_CUDA
    Vector<RestrictionBlockTag> tags;
    AMREX_ALWAYS_ASSERT(result.size() == fine_indices.size());
    for (MFIter mfi(result, false); mfi.isValid(); ++mfi) {
        const int fi = fine_indices.at(mfi.index());
        const int local_fine = fine.localindex(fi);
        AMREX_ALWAYS_ASSERT(local_fine >= 0);
        const auto fab = OSI::MakeOsiFabContext(
                             amrex::grow(fine.boxArray()[fi], fine.nGrowVect()), 0)
                             .fab;
        const Box region = mfi.validbox();
        for (Long parent = 0; parent < region.numPts(); parent += restriction_parents_per_block) {
            tags.push_back({region, fab, parent, mfi.LocalIndex(), local_fine,
                            phases ? phases->slot(fi) : -1});
        }
    }
    work.resize(tags.size());
    Gpu::copy(Gpu::hostToDevice, tags.begin(), tags.end(), work.begin());
#endif
}

void AmrCoreLBM::RemakeDdfState(
    int lev, amrex::Real time, amrex::MultiFab& new_old_state) {
    amrex::MultiFab& mf = new_old_state;
    amrex::MultiFab& f_old_lev_f = f_old[lev];
    amrex::MultiFab& f_old_lev_c = f_old[lev - 1];
    const amrex::Real scale = tau[lev] / tau[lev - 1] / 2.0;
    const amrex::IntVect fill_ng(0);
    const auto ratio = refRatio(lev - 1);
    AMREX_ALWAYS_ASSERT(ratio == amrex::IntVect(2));
    const int interp_mode_local = interp_mode;
    Interpolater* interp_mapper = DdfInterpolater();
    const auto coarsener = interp_mapper->BoxCoarsener(ratio);

    // Regrid 时目标 old_state 的布局不同于 f_old[lev]。此处按 FPinfo
    // 构造临时 coarse/fine patch：旧 fine valid 数据优先迁移，新增 valid
    // 新布局中缺少旧 fine 来源的 valid 区域由 coarse 插值补全。
    const auto& fpc = FabArrayBase::TheFPinfo(
        f_old_lev_f, mf, fill_ng, coarsener,
        Geom(lev), Geom(lev - 1), nullptr); // 比较旧 fine 布局 f_old_lev_f 和新 fine 布局 mf，找出新布局中不能从旧 fine 数据直接获得、必须由 coarse 插值填充的区域，并生成相应的 coarse stencil 布局信息。
    perf_stats.interp_fillpatch_boxes +=
        static_cast<long long>(fpc.ba_fine_patch.size());
    for (int i = 0; i < fpc.ba_fine_patch.size(); ++i) {
        perf_stats.interp_fillpatch_fine_cells +=
            fpc.ba_fine_patch[i].numPts();
    }
    for (int i = 0; i < fpc.ba_crse_patch.size(); ++i) {
        perf_stats.interp_fillpatch_coarse_cells +=
            fpc.ba_crse_patch[i].numPts();
    }

    {
        ScopedPerfTimer timer(perf_stats.interp_fillpatch);

        if (!fpc.ba_crse_patch.empty()) { // ba_fine_patch[n]：需要写入的 fine 区域
            // ba_crse_patch[n]：插值该 fine 区域需要读取的 coarse stencil
            amrex::MultiFab coarse_patch(
                fpc.ba_crse_patch, fpc.dm_patch, Q, 0, amrex::MFInfo(),
                *fpc.fact_crse_patch);
            amrex::MultiFab fine_patch(
                fpc.ba_fine_patch, fpc.dm_patch, Q, 0, amrex::MFInfo(),
                *fpc.fact_fine_patch);

            coarse_patch.setDomainBndry(
                std::numeric_limits<Real>::quiet_NaN(), Geom(lev - 1)); // 位于非周期物理域外的单元设置为 NaN
            coarse_patch.ParallelCopy(
                f_old_lev_c, 0, 0, Q, amrex::IntVect(0),
                amrex::IntVect(0), Geom(lev - 1).periodicity());
            if (Gpu::inLaunchRegion()) {
                GpuBndryFuncFab<AmrCoreFill> gpu_bndry_func(AmrCoreFill{}); // 创建一个名为 gpu_bndry_func 的 GPU 边界函数包装器，并用临时的 AmrCoreFill 对象初始化它。
                PhysBCFunct<GpuBndryFuncFab<AmrCoreFill>> cphysbc(
                    geom[lev - 1], bcs, gpu_bndry_func);
                cphysbc(
                    coarse_patch, 0, Q, coarse_patch.nGrowVect(),
                    time, 0); // 根据物理边界条件填充coarse_patch 的 valid Box物理区域之外的stencil cell
            } else {
                CpuBndryFuncFab bndry_func(nullptr);
                PhysBCFunct<CpuBndryFuncFab> cphysbc(
                    geom[lev - 1], bcs, bndry_func);
                cphysbc(
                    coarse_patch, 0, Q, coarse_patch.nGrowVect(),
                    time, 0);
            }

            // 只在实际 coarse interpolation patch 上做非平衡 DDF 缩放。
            for (MFIter mfi(coarse_patch, false); mfi.isValid(); ++mfi) {
                auto coarse = coarse_patch.array(mfi);
                const Box bx = mfi.validbox();
                amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                    average_scale(i, j, k, coarse, scale);
                });
            }

            for (MFIter mfi(fine_patch, false); mfi.isValid(); ++mfi) {
                const auto fine = fine_patch.array(mfi);
                const auto coarse = coarse_patch.const_array(mfi);
                const Box bx = mfi.validbox();
                if (interp_mode_local == 0) {
                    amrex::ParallelFor(
                        bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                            interp_bilinear_ddf(i, j, k, fine, coarse);
                        });
                } else if (interp_mode_local == 1) {
                    const Box coarse_parent_box = amrex::coarsen(bx, 2);
                    amrex::ParallelFor(
                        coarse_parent_box,
                        [=] AMREX_GPU_DEVICE(int ic, int jc, int kc) {
                            interp_cell_cons_linear_children_ddf(
                                ic, jc, kc, fine, coarse, bx);
                        });
                } else {
                    const Box coarse_parent_box = amrex::coarsen(bx, 2);
                    amrex::ParallelFor(
                        coarse_parent_box,
                        [=] AMREX_GPU_DEVICE(int ic, int jc, int kc) {
                            interp_cell_quadratic_children_ddf(
                                ic, jc, kc, fine, coarse, bx);
                        });
                }
            }

            mf.ParallelCopy(fine_patch, 0, 0, Q, amrex::IntVect(0),
                            fill_ng, Geom(lev).periodicity());
        }

        mf.ParallelCopy(f_old_lev_f, 0, 0, Q, amrex::IntVect(0),
                        fill_ng, Geom(lev).periodicity()); // 把 regrid 前旧 fine 网格中仍然有效的数据，迁移到 regrid 后的新 fine 布局。
    }
}

void AmrCoreLBM::FillMacroPatch(int lev, amrex::Real time, amrex::MultiFab& mf) {
    Interpolater* mapper = &cell_cons_interp;

    if (lev == 0) {
        amrex::MultiFab& u_lev = velocity[lev];
        amrex::Vector<amrex::MultiFab*> cmf{&u_lev};
        amrex::Vector<Real> ctime{time};

        if (Gpu::inLaunchRegion()) {
            GpuBndryFuncFab<AmrCoreFill> gpu_bndry_func(AmrCoreFill{});
            PhysBCFunct<GpuBndryFuncFab<AmrCoreFill>> physbc(geom[lev], bcs, gpu_bndry_func);
            FillPatchSingleLevel(mf, time, cmf, ctime, 0, 0, AMREX_SPACEDIM, geom[lev], physbc, 0);
        } else {
            CpuBndryFuncFab bndry_func(nullptr);
            PhysBCFunct<CpuBndryFuncFab> physbc(geom[lev], bcs, bndry_func);
            FillPatchSingleLevel(mf, time, cmf, ctime, 0, 0, AMREX_SPACEDIM, geom[lev], physbc, 0);
        }
    } else {
        amrex::MultiFab& u_lev_c = velocity[lev - 1];
        amrex::MultiFab& u_lev_f = velocity[lev];

        amrex::Vector<amrex::MultiFab*> cmf{&u_lev_c};
        amrex::Vector<amrex::MultiFab*> fmf{&u_lev_f};

        amrex::Vector<Real> ctime{time};
        amrex::Vector<Real> ftime{time};

        if (Gpu::inLaunchRegion()) {
            GpuBndryFuncFab<AmrCoreFill> gpu_bndry_func(AmrCoreFill{});
            PhysBCFunct<GpuBndryFuncFab<AmrCoreFill>> cphysbc(geom[lev - 1], bcs, gpu_bndry_func);
            PhysBCFunct<GpuBndryFuncFab<AmrCoreFill>> fphysbc(geom[lev], bcs, gpu_bndry_func);
            amrex::FillPatchTwoLevels(mf, time, cmf, ctime, fmf, ftime, 0, 0, AMREX_SPACEDIM,
                                      geom[lev - 1], geom[lev], cphysbc, 0, fphysbc, 0,
                                      refRatio(lev - 1), mapper, bcs, 0);
        } else {
            CpuBndryFuncFab bndry_func(nullptr);
            PhysBCFunct<CpuBndryFuncFab> cphysbc(geom[lev - 1], bcs, bndry_func);
            PhysBCFunct<CpuBndryFuncFab> fphysbc(geom[lev], bcs, bndry_func);

            amrex::FillPatchTwoLevels(mf, time, cmf, ctime, fmf, ftime, 0, 0, AMREX_SPACEDIM,
                                      geom[lev - 1], geom[lev], cphysbc, 0, fphysbc, 0,
                                      refRatio(lev - 1), mapper, bcs, 0);
        }
    }
}

void AmrCoreLBM::RefineMesh(amrex::Real cur_time) { // 根据流场特征重新生成 AMR 网格,并把所有依赖旧网格拓扑的缓存同步重建。
    ClearOsiTransferCaches();
    for (auto& cache : boundary_work_cache) {
        cache.clear();
    }
    regrid_tag_counts.assign(max_level + 1, -1);
    for (auto& cache : interpolation_cache) {
        cache.clear();
    }
    for (auto& cache : average_down_cache) {
        cache.clear();
    }
    regrid(0, cur_time);
    RebuildCoarseFineCaches();
    bool refinement_diagnostics = false;
    ParmParse("verification").query("refinement_diagnostics", refinement_diagnostics);
    if (refinement_diagnostics) {
        amrex::Print() << "REFINEMENT_REGRID time=" << cur_time << '\n';
        PrintRegridDiagnostics();
    }
    bool regrid_valid_check = false;
    ParmParse("verification").query("regrid_valid_check", regrid_valid_check);
    if (streamMode() == 1 && (osiReferenceEnabled() || regrid_valid_check)) {
        for (int lev = 0; lev <= finest_level; ++lev) {
            const MultiFab& state = osi_state.at(lev);
            MultiFab canonical(state.boxArray(), state.DistributionMap(), Q, 0);
            bool nonfinite = false;
            DecodeOsiValid(state, osi_phase.at(lev), canonical);
            nonfinite = canonical.contains_nan(0, Q, 0) ||
                        canonical.contains_inf(0, Q, 0);
            amrex::Print() << "regrid_valid_check level=" << lev
                           << " boxes=" << state.boxArray().size()
                           << " all_valid_finite=" << (nonfinite ? 0 : 1)
                           << '\n';
            AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
                !nonfinite, "Regrid produced nonfinite valid OSI DDF");
        }
    }
}

void AmrCoreLBM::ClearOsiTransferCaches() {
    // 任一层存储改变前，先失效所有跨层 Array4 和设备任务。
    for (auto& cache : interpolation_cache) {
        cache.clear_work();
    }
    for (auto& cache : average_down_cache) {
        cache.clear_work();
    }
}

void AmrCoreLBM::RebuildCoarseFineCaches() {
    ClearOsiTransferCaches();
    // 网格初始化或 regrid 完成后，各层的 BoxArray 和 DistributionMapping
    // 可能已经改变。所有依赖旧网格拓扑、Box 编号或数据归属的缓存都必须
    // 在再次执行 FillGhost、Boundary 和 AverageDown 前统一重建。

    // 重新统计每个 coarse level 被 fine level 覆盖的单元数，以及覆盖区中
    // 紧邻 coarse-fine 交界面的单元数；未被实际建立的层保持为 0。
    covered_cell_counts.assign(max_level + 1, 0);
    interface_cell_counts.assign(max_level + 1, 0);

    // 先释放所有由旧网格布局生成的 MultiFab、Box 列表和索引映射，避免
    // 后续计算继续使用已经失效的 Box 编号、owner 或 coarse/fine 对应关系。
    for (int lev = 0; lev <= max_level; ++lev) {
        covered_mask[lev].clear();
        interface_mask[lev].clear();
        interpolation_cache[lev].clear();
        average_down_cache[lev].clear();
        boundary_work_cache[lev].clear();
    }

    // 构造 coarse 覆盖掩码和 coarse-fine 交界掩码，供粗层
    // Collide/Stream 跳过被细层替代的区域，并供交界限制操作使用。
    RebuildCoarseFineMasks();

    // 预先找出各 fine Fab 位于非周期物理边界上的互不重叠工作 Box，
    // Boundary() 批量执行缓存任务，不必在每个时间步重复分析几何关系。
    BuildBoundaryWorkCache();

    // 为每个 fine ghost work_box 建立所需的 coarse stencil staging Fab、
    // fine Fab 索引和 owner 映射，供 FillDdfGhostFromCoarse() 直接插值。
    // stencil 大小由当前 interp_mode 对应的 Interpolater::BoxCoarsener 决定。
    BuildInterpolationCache();

    // 建立 fine-to-coarse 平均下传缓存：全覆盖模式使用完整 coarse 缓冲区，
    // 稀疏模式只保存 coarse-fine 交界条带及其对应的 fine Fab 索引。
    BuildAverageCache();
}

void AmrCoreLBM::RebuildCoarseFineMasks() {
    if (streamMode() == 1) {
        RebuildCoarseFineMasksForState(osi_state);
    } else {
        RebuildCoarseFineMasksForState(f_old);
    }
}

void AmrCoreLBM::RebuildCoarseFineMasksForState(
    const Vector<MultiFab>& state) {
    for (int lev = 0; lev < finest_level; ++lev) {
        AMREX_ALWAYS_ASSERT(state.at(lev).isDefined());
        AMREX_ALWAYS_ASSERT(state.at(lev + 1).isDefined());
        covered_mask[lev] = amrex::makeFineMask(
            // interface 的 valid 分类只需 3x3x3 邻域；但碰撞会在 DDF
            // 的完整 grown ring 中读取 covered 标记，故宽度仍须为 nghost。
            state[lev], state[lev + 1], amrex::IntVect(nghost), refRatio(lev),
            Geom(lev).periodicity(), 0, 1);

        // interface 的 ghost 仅是 valid 标记的同层/周期副本，宽度与
        // 碰撞访问的 DDF ghost 一致。
        interface_mask[lev].define(
            state[lev].boxArray(), state[lev].DistributionMap(), 1, nghost);
        interface_mask[lev].setVal(0);
        // 只允许域内或周期映射的邻居参与界面判定，排除非周期域外邻居。
        const Box neighbor_domain = Geom(lev).growPeriodicDomain(1);

        for (MFIter mfi(interface_mask[lev], TilingIfNotGPU()); mfi.isValid(); ++mfi) {
            // interface 的物理分类只在 valid 单元构造；其 ghost 随后由
            // FillBoundary 从同层或周期对应的 valid 单元复制。
            const Box bx = mfi.tilebox();
            const Array4<const int>& covered = covered_mask[lev].const_array(mfi);
            const Array4<int>& interface = interface_mask[lev].array(mfi);

            amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                if (covered(i, j, k) == 0) {
                    return;
                }

                for (int dk = AMREX_D_PICK(0, 0, -1); dk <= AMREX_D_PICK(0, 0, 1); ++dk) {
                    for (int dj = -1; dj <= 1; ++dj) {
                        for (int di = -1; di <= 1; ++di) {
                            const int ni = i + di;
                            const int nj = j + dj;
                            const int nk = k + dk;
                            if (!neighbor_domain.contains(IntVect(AMREX_D_DECL(ni, nj, nk)))) {
                                continue;
                            }
                            if (covered(ni, nj, nk) == 0) {
                                interface(i, j, k) = 1;
                                return;
                            }
                        }
                    }
                }
            });
        }
        amrex::Gpu::streamSynchronize();
        interface_mask[lev].FillBoundary(Geom(lev).periodicity());

        covered_cell_counts[lev] = covered_mask[lev].sum(0, 0);
        interface_cell_counts[lev] = interface_mask[lev].sum(0, 0);
    }
}

void AmrCoreLBM::BuildBoundaryWorkCache() {
    for (int lev = 0; lev <= finest_level; ++lev) {
        const BoxArray& ba = boxArray(lev);
        const Box domain = Geom(lev).Domain();
        const auto is_periodic = Geom(lev).isPeriodicArray();
        auto& cache = boundary_work_cache[lev];
        cache.clear();
        Vector<BoundaryWorkTag> advance_tags;
        Vector<BoundaryWorkTag> repair_tags;
        const bool use_osi = streamMode() == 1;
        const MultiFab& state = use_osi ? osi_state.at(lev) : f_new.at(lev);
        for (MFIter mfi(state, false); mfi.isValid(); ++mfi) {
            const Box& valid_box = ba[mfi.index()];
            const auto fab = OSI::MakeOsiFabContext(
                                 amrex::grow(valid_box, state.nGrowVect()), 0)
                                 .fab;
            const int phase_index = use_osi
                                        ? osi_fab_phase_cache.at(lev).slot(mfi.index())
                                        : -1;
            cache.full_cells += valid_box.numPts();

            // 边界条件同样会更新域内Fab对象中、紧邻非周期物理边界的ghost单元。
            const Box boundary_source_box = amrex::grow(valid_box, nghost) & domain;
            BoxList physical_faces;
            for (int face_dir = 0; face_dir < AMREX_SPACEDIM; ++face_dir) {
                if (is_periodic[face_dir]) {
                    continue;
                }
                for (const int side : {domain.smallEnd(face_dir), domain.bigEnd(face_dir)}) {
                    if (side < boundary_source_box.smallEnd(face_dir) ||
                        side > boundary_source_box.bigEnd(face_dir)) {
                        continue;
                    }
                    // 直接处理域内物理面，包含当前 Fab 的 valid 与 ghost。
                    Box face = boundary_source_box;
                    face.setSmall(face_dir, side);
                    face.setBig(face_dir, side);
                    physical_faces.push_back(face);
                }
            }
            const BoxList disjoint_faces = amrex::removeOverlap(physical_faces);
            for (const Box& box : disjoint_faces) {
                advance_tags.push_back({box, fab, mfi.LocalIndex(), phase_index});
                cache.advance_cells += box.numPts();
                const Box valid_part = box & valid_box;
                if (valid_part.ok()) {
                    repair_tags.push_back({valid_part, fab, mfi.LocalIndex(), phase_index});
                    cache.repair_cells += valid_part.numPts();
                }
            }
        }
        cache.advance_tags.define(advance_tags);
        cache.repair_tags.define(repair_tags);
    }
}

void AmrCoreLBM::BuildInterpolationCache() {
    for (int lev = 1; lev <= finest_level; ++lev) {
        const double start = amrex::second();
        AMREX_ALWAYS_ASSERT(lev > 0 && lev <= max_level);

        const MultiFab& fine_layout =
            streamMode() == 1 ? osi_state.at(lev) : f_old.at(lev);
        const auto fill_ng = fine_layout.nGrowVect();
        const BoxArray& fine_ba = fine_layout.boxArray();
        const BoxArray fine_ba_simplified = fine_ba.simplified();
        const MultiFab& coarse_layout =
            streamMode() == 1 ? osi_state.at(lev - 1) : f_old.at(lev - 1);
        Box fine_domain = Geom(lev).Domain();
        for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
            if (Geom(lev).isPeriodic(dir)) {
                fine_domain.grow(dir, fill_ng[dir]); // 根据周期性条件扩展 fine_domain 的边界。
            }
        }

        auto& coarse_stage = interpolation_cache[lev].coarse_stage;
        auto& patches = interpolation_cache[lev].patches;
        interpolation_cache[lev].clear();

        for (int fine_index = 0; fine_index < fine_ba.size(); ++fine_index) {
            const Box target =
                amrex::grow(fine_ba[fine_index], fill_ng) & fine_domain;
            const BoxList leftover =
                fine_ba_simplified.complementIn(target, Geom(lev).periodicity()); // 计算 target 中没有被 fine_ba_simplified 覆盖的区域, 并以多个互不重叠的 Box 返回，考虑周期性情况。
            for (const Box& work_box : leftover) {
                patches.push_back({work_box, fine_index});
            }
        }

        BuildInterpolationStage(
            lev, coarse_layout, fine_layout, patches, coarse_stage);
        if (streamMode() == 1 && coarse_stage.isDefined()) {
            interpolation_cache.at(lev).copy.define(
                osi_state.at(lev - 1), coarse_stage,
                Geom(lev - 1).periodicity(), Q, osi_mpi_device_direct,
                osi_fab_phase_cache.at(lev - 1)); // 构建粗网格复制所需的缓存信息
        }
        if (coarse_stage.isDefined()) {
            BuildInterpolationWork(coarse_stage, fine_layout, patches,
                                   interpolation_cache.at(lev).work,
                                   streamMode() == 1 ? &osi_fab_phase_cache.at(lev) : nullptr);
        }
        ++perf_stats.interp_cache_builds;
        perf_stats.interp_cache_build += amrex::second() - start;
    }
}

void AmrCoreLBM::BuildAverageCache() {
    for (int lev = 0; lev < finest_level; ++lev) {
        average_down_cache.at(lev).clear();
        const MultiFab& fine_layout =
            streamMode() == 1 ? osi_state.at(lev + 1) : f_old.at(lev + 1);
        const BoxArray coarse_from_fine =
            amrex::coarsen(fine_layout.boxArray(), refRatio(lev));

        BoxList interface_boxes;
        Vector<int> interface_owners;
        Vector<int> fine_box_indices;
        const auto& periodicity = Geom(lev).periodicity();
        const auto& fine_dm = fine_layout.DistributionMap();
        for (int ibox = 0; ibox < coarse_from_fine.size(); ++ibox) {
            const Box& covered_box = coarse_from_fine[ibox];
            // 排除非周期域外邻居，保留周期方向的覆盖查询，与掩码判定一致。
            const Box search_box = amrex::grow(covered_box, 1) &
                                   Geom(lev).growPeriodicDomain(1);
            const BoxList uncovered =
                coarse_from_fine.complementIn(search_box, periodicity);
            BoxList candidates;
            for (const Box& uncovered_box : uncovered) {
                const Box interface_box =
                    amrex::grow(uncovered_box, 1) & covered_box;
                if (interface_box.ok()) {
                    candidates.push_back(interface_box);
                }
            }

            const BoxList disjoint = amrex::removeOverlap(candidates);
            for (const Box& interface_box : disjoint) {
                interface_boxes.push_back(interface_box);
                interface_owners.push_back(fine_dm[ibox]);
                fine_box_indices.push_back(ibox);
            }
        }

        BoxArray interface_ba(interface_boxes);
        if (!interface_ba.empty()) {
            DistributionMapping interface_dm(interface_owners);
            average_down_cache[lev].interface_buffer.define(interface_ba, interface_dm, Q, 0);
            average_down_cache[lev].fine_box_indices = std::move(fine_box_indices);
            if (streamMode() == 1) {
                average_down_cache.at(lev).copy.define(
                    average_down_cache.at(lev).interface_buffer, osi_state.at(lev),
                    periodicity, Q, osi_mpi_device_direct,
                    osi_fab_phase_cache.at(lev));
            }
            BuildRestrictionWork(fine_layout, average_down_cache.at(lev).interface_buffer,
                                 average_down_cache.at(lev).fine_box_indices, average_down_cache.at(lev).block_tags,
                                 streamMode() == 1 ? &osi_fab_phase_cache.at(lev + 1) : nullptr);
        }
    }
}

void AmrCoreLBM::FindCentre() {

    for (int p_num = 0; p_num < particle_num; p_num++) {
        points[p_num] = particles[p_num]->ReturnCentre();
    }
}

void AmrCoreLBM::MakeNewLevelFromCoarse(int lev, amrex::Real time, const amrex::BoxArray& ba,
                                        const amrex::DistributionMapping& dm) {
    if (probe_.regrid_fixture) {
        amrex::Print() << "AMR_LIFECYCLE MakeNewLevelFromCoarse level=" << lev << '\n';
    }

    // 给新建的细网格层 lev 分配数据，并从紧邻的粗层插值出初始 DDF 状态, 只在 regrid() 新增一个此前不存在的细层时调用，RefineMesh() 会调用。
    if (lev == 0) {
        amrex::Abort("Cannot construct level 0 from a coarser level.");
    }

    amrex::MultiFab& u_lev = velocity.at(lev);
    amrex::MultiFab& rho_lev = density.at(lev);
    amrex::MultiFab& vort_lev = vorticity.at(lev);
    amrex::MultiFab& force_lev = force.at(lev);
    amrex::MultiFab& shear_lev = shear.at(lev);

    u_lev.define(ba, dm, AMREX_SPACEDIM, nghost);
    rho_lev.define(ba, dm, 1, nghost);
    vort_lev.define(ba, dm, 2, nghost); // 改成两个，分别存vort和q
    force_lev.define(ba, dm, AMREX_SPACEDIM, nghost);
    shear_lev.define(ba, dm, 1, nghost);

    if (streamMode() == 0) {
        amrex::MultiFab& f_new_lev = f_new.at(lev);
        amrex::MultiFab& f_old_lev = f_old.at(lev);
        f_new_lev.define(ba, dm, Q, nghost);
        f_old_lev.define(ba, dm, Q, nghost);

        InitializeNewLevelFromCoarse(lev, time);
    } else {
        InitializeOsiLevel(lev, ba, dm);
        amrex::MultiFab& state = osi_state.at(lev);
        state.setVal(std::numeric_limits<Real>::quiet_NaN());

        InitializeNewLevelFromCoarse(lev, time);
        if (osiReferenceEnabled()) {
            // 新层的 A-B 参考态独立从粗层 canonical 数据插值，供重网格后逐层核对。
            MultiFab& reference = f_old.at(lev);
            reference.define(ba, dm, Q, nghost);
            f_new.at(lev).define(ba, dm, Q, nghost);
            InitializeDdfFromCoarse(
                lev, time, f_old.at(lev - 1), false, 0, reference);
        }
    }

    force_lev.setVal(0.0, nghost);
    shear_lev.setVal(0.0, nghost);
    vort_lev.setVal(0.0, nghost);
}

void AmrCoreLBM::RemakeLevel(int lev, amrex::Real time, const amrex::BoxArray& ba,
                             const amrex::DistributionMapping& dm) {
    if (probe_.regrid_fixture) {
        amrex::Print() << "AMR_LIFECYCLE RemakeLevel level=" << lev << '\n';
    }
    // 某个已存在的细层网格布局发生变化后，按新的 ba/dm 重建该层，并把旧流场尽可能迁移过去。主要由RefineMesh()调用
    amrex::MultiFab u_new(ba, dm, AMREX_SPACEDIM, nghost); // 按新 ba/dm 重建派生宏观场；density/velocity 后续由 DDF 重新计算。
    amrex::MultiFab rho_new(ba, dm, 1, nghost);
    amrex::MultiFab vort_new(ba, dm, 2, nghost);
    amrex::MultiFab force_new(ba, dm, AMREX_SPACEDIM, nghost);
    amrex::MultiFab shear_new(ba, dm, 1, nghost);

    if (streamMode() == 1) {
        ++perf_stats.interp_regrid_fill_calls;
        ScopedPerfTimer timer(perf_stats.interp_regrid_fill);

        const MultiFab& current_state = osi_state.at(lev);
        const bool same_layout =
            current_state.isDefined() &&
            current_state.boxArray() == ba &&
            current_state.DistributionMap() == dm;

        if (same_layout) {
            // regrid 没有改变该层的 Fab 布局时，保留现有 raw 数据和 phase；
            // 直接清零 phase 会改变 raw 地址的物理含义。
            force_new.setVal(0.0, nghost);
            shear_new.setVal(0.0, nghost);
            vort_new.setVal(0.0, nghost);

            std::swap(u_new, velocity[lev]);
            std::swap(rho_new, density[lev]);
            std::swap(vort_new, vorticity[lev]);
            std::swap(force_new, force[lev]);
            std::swap(shear_new, shear[lev]);
            return;
        }

        MultiFab old_state;
        // 先让含旧 MultiFab Array4 的同层任务失效，再交换状态。
        osi_fillboundary_cache.at(lev).clear();
        std::swap(old_state, osi_state.at(lev));

        const auto old_phase = osi_phase.at(lev);

        InitializeOsiLevel(lev, ba, dm);

        MultiFab& new_osi_state = osi_state.at(lev);
        new_osi_state.setVal(std::numeric_limits<Real>::quiet_NaN());

        // 旧、新 fine 布局重叠区直接进行 phase-aware raw remap；新布局从
        // phase=0 开始，避免为重构路径分配 canonical sync buffer。
        ParallelCopyOsi(lev, old_state, old_phase, new_osi_state, 0);

        if (lev > 0) {
            const IntVect fill_ng(0);
            const auto ratio = refRatio(lev - 1);
            const auto coarsener = DdfInterpolater()->BoxCoarsener(ratio);
            const auto& fpc = FabArrayBase::TheFPinfo(
                old_state, new_osi_state, fill_ng, coarsener,
                Geom(lev), Geom(lev - 1), nullptr); // 得到新 fine 中不被旧 fine 覆盖的区域以及为了插值这些 fine 区域需要读取的粗层 stencil

            // patch 索引不同于目标 Fab 索引；求交建立直接写回的区域与 owner。
            Vector<Box> work_boxes;
            Vector<int> fine_indices;
            for (int patch = 0; patch < fpc.ba_fine_patch.size(); ++patch) {
                for (const auto& entry : ba.intersections(fpc.ba_fine_patch[patch])) {
                    work_boxes.push_back(entry.second);
                    fine_indices.push_back(entry.first);
                }
            }
            if (!work_boxes.empty()) {
                InitializeDdfFromCoarse(
                    lev, time, osi_state.at(lev - 1), true,
                    osi_phase.at(lev - 1), new_osi_state,
                    work_boxes, fine_indices);
            }
        }

        if (osiReferenceEnabled()) {
            // 锁步参考态也必须迁移到新布局，不能用旧 Array4 比较重构后的 OSI。
            // 复用独立 canonical 重构路径，保留其作为数值对照的意义。
            MultiFab reference(ba, dm, Q, nghost);
            RemakeDdfState(lev, time, reference);
            std::swap(reference, f_old.at(lev));
            f_new.at(lev).define(ba, dm, Q, nghost);
        }

        force_new.setVal(0.0, nghost);
        shear_new.setVal(0.0, nghost);
        vort_new.setVal(0.0, nghost);

        std::swap(u_new, velocity[lev]);
        std::swap(rho_new, density[lev]);
        std::swap(vort_new, vorticity[lev]);
        std::swap(force_new, force[lev]);
        std::swap(shear_new, shear[lev]);
        return;
    }

    amrex::MultiFab new_state(ba, dm, Q, nghost);
    amrex::MultiFab old_state(ba, dm, Q, nghost);

    {
        ScopedPerfTimer timer(perf_stats.interp_regrid_fill);
        ++perf_stats.interp_regrid_fill_calls;
        RemakeDdfState(lev, time, old_state);
    }

    std::swap(new_state, f_new[lev]);
    std::swap(old_state, f_old[lev]);

    std::swap(u_new, velocity[lev]);
    std::swap(rho_new, density[lev]);
    std::swap(vort_new, vorticity[lev]);
    std::swap(force_new, force[lev]);
    std::swap(shear_new, shear[lev]);

    force[lev].setVal(0.0, nghost);
    shear[lev].setVal(0.0, nghost);
    vorticity[lev].setVal(0.0, nghost);
}

void AmrCoreLBM::InitializeOsiLevel(
    int lev, const BoxArray& ba, const DistributionMapping& dm) {
    osi_fillboundary_cache.at(lev).clear();
    osi_state.at(lev).define(ba, dm, Q, nghost);
    osi_phase.at(lev) = 0;
    osi_fab_phase_cache.at(lev).define(osi_state.at(lev), 0);
    BuildOsiCommunicationRegionCache(lev);
}

void AmrCoreLBM::InitializeNewLevelFromCoarse(int lev, Real time) {
    AMREX_ALWAYS_ASSERT(lev > 0);
    const bool use_osi = streamMode() == 1;
    if (use_osi) {
        AMREX_ALWAYS_ASSERT(osi_phase.at(lev) == 0);
    }

    MultiFab& fine_state =
        use_osi ? osi_state.at(lev) : f_old.at(lev);
    const MultiFab& coarse_state =
        use_osi ? osi_state.at(lev - 1) : f_old.at(lev - 1);
    InitializeDdfFromCoarse(
        lev, time, coarse_state, use_osi,
        use_osi ? osi_phase.at(lev - 1) : 0, fine_state);
}

void AmrCoreLBM::InitializeDdfFromCoarse(
    int lev, Real time, const MultiFab& coarse_state,
    bool coarse_osi, std::uint64_t coarse_phase, MultiFab& fine_state,
    const Vector<Box>& requested_boxes, const Vector<int>& requested_indices) {
    (void)time;
    // 新层只构造 valid DDF；OSI 目标 phase=0，此时地址等同 canonical。
    // 不初始化 fine ghost，其生命周期由后续 FillGhostLevel、同层通信
    // 和物理边界处理维护。粗层模板的域外延拓仍用于 valid 边缘插值。
    AMREX_ALWAYS_ASSERT(requested_boxes.size() == requested_indices.size());
    Vector<InterpolationPatchTag> patches;
    if (requested_boxes.empty()) {
        for (int index = 0; index < fine_state.boxArray().size(); ++index) {
            patches.push_back({fine_state.boxArray()[index], index});
        }
    } else {
        for (int index = 0; index < static_cast<int>(requested_boxes.size()); ++index) {
            patches.push_back({requested_boxes[index], requested_indices[index]});
        }
    }
    MultiFab coarse_stage;
    BuildInterpolationStage(lev, coarse_state, fine_state, patches, coarse_stage);

    if (coarse_osi) {
        // 调用内搬运计划只绑定当前临时模板，不进入长期 ghost 缓存。
        ParallelCopyOsi(lev - 1, coarse_state, coarse_phase, coarse_stage, 0);
    } else {
        coarse_stage.ParallelCopy(
            coarse_state, 0, 0, Q, IntVect(0), IntVect(0),
            Geom(lev - 1).periodicity());
    }
    ApplyPhysicalBoundaryToInterpolationStage(
        lev - 1, coarse_stage);
    const Real scale = tau.at(lev) / tau.at(lev - 1) / Real(2.0);
    // 初始化目标为 canonical 或 phase=0 OSI，和正常推进共用建表与批量执行器。
    // 临时任务必须在 stage/fine 存储释放或替换前完成，不进入持久 ghost 缓存。
    InterpolationBatchCache work;
    BuildInterpolationWork(coarse_stage, fine_state, patches, work);
    InterpolateDdfStageBatch<false>(coarse_stage, fine_state, work, nullptr, true, scale);
    Gpu::streamSynchronize();
}

void AmrCoreLBM::ClearLevel(int lev) {
    if (probe_.regrid_fixture) {
        amrex::Print() << "AMR_LIFECYCLE ClearLevel level=" << lev << '\n';
    }

    ClearOsiTransferCaches();
    boundary_work_cache[lev].clear();
    osi_fillboundary_cache[lev].clear();
    osi_fab_phase_cache[lev].clear();
    f_old[lev].clear();
    f_new[lev].clear();
    osi_state[lev].clear();
    osi_phase[lev] = 0;
    velocity[lev].clear();
    vorticity[lev].clear();
    density[lev].clear();
    shear[lev].clear();
    force[lev].clear();
}

void AmrCoreLBM::MakeNewLevelFromScratch(int lev, amrex::Real time, const amrex::BoxArray& ba,
                                         const amrex::DistributionMapping& dm) {
    // 作用“拿到已经规划好的某层网格后，，并从“初始物理条件”生成数据”, InitMesh()会调用到它
    /* main.cpp
  └─ lid.InitMesh(cur_time)
       └─ AmrCoreLBM::InitMesh()
            └─ AMReX::InitFromScratch()
                 └─ AMReX::MakeNewGrids()
                      ├─ 生成 level 0 的 BoxArray/DistributionMapping
                      ├─ MakeNewLevelFromScratch(0, ...)
                      ├─ ErrorEst() 标记加密区域
                      ├─ 生成 level 1 的网格
                      └─ MakeNewLevelFromScratch(1, ...) */

    amrex::MultiFab& u_lev = velocity.at(lev);
    amrex::MultiFab& rho_lev = density.at(lev);
    amrex::MultiFab& vort_lev = vorticity.at(lev);
    amrex::MultiFab& force_lev = force.at(lev);
    amrex::MultiFab& shear_lev = shear.at(lev);

    u_lev.define(ba, dm, AMREX_SPACEDIM, nghost);
    rho_lev.define(ba, dm, 1, nghost);
    vort_lev.define(ba, dm, 2, nghost);
    force_lev.define(ba, dm, AMREX_SPACEDIM, nghost);
    shear_lev.define(ba, dm, 1, nghost);
    const bool allocate_ab = canonicalStateEnabled();
    if (allocate_ab) {
        amrex::MultiFab& f_new_lev = f_new.at(lev);
        amrex::MultiFab& f_old_lev = f_old.at(lev);
        f_new_lev.define(ba, dm, Q, nghost);
        f_old_lev.define(ba, dm, Q, nghost);
    }
    if (streamMode() == 1) {
        // valid 与两层 ghost 共同组成 Fab-local OSI 保护环。ghost 是重叠逻辑
        // 副本，碰撞后按当前 phase 分批同步；它不承担跨 Fab streaming 写入目标。
        InitializeOsiLevel(lev, ba, dm);
    }

    force_lev.setVal(0.0, nghost);
    shear_lev.setVal(0.0, nghost);
    vort_lev.setVal(0.0, nghost);

    if (allocate_ab) {
        amrex::MultiFab& f_new_lev = f_new.at(lev);
        amrex::MultiFab& f_old_lev = f_old.at(lev);
        for (MFIter mfi(f_old_lev, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
            const Box& bx = mfi.growntilebox(nghost);
            const Array4<Real>& fold = f_old_lev.array(mfi);
            const Array4<Real>& fnew = f_new_lev.array(mfi);
            const bool seed_pattern = osi_verification_pattern;

            amrex::ParallelFor(bx, Q, [=] AMREX_GPU_DEVICE(int i, int j, int k, int q) {
                if (seed_pattern) {
                    const int code = (13 * i + 7 * j + 3 * k + 5 * q) % 97;
                    const Real perturbation = Real(1.0e-4) * Real(code - 48);
                    const Real value = w[q] * (Real(1.0) + perturbation);
                    fold(i, j, k, q) = value;
                    fnew(i, j, k, q) = value;
                } else if (q == 0) {
                    init_fluid(i, j, k, fold, fnew);
                }
            });
        }
    }

    if (streamMode() == 1) {
        amrex::MultiFab& state = osi_state[lev];
        state.setVal(std::numeric_limits<Real>::quiet_NaN());
        for (MFIter mfi(state, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
            const Box& bx = mfi.tilebox();
            const Array4<Real>& dst = state.array(mfi);
            const bool seed_pattern = osi_verification_pattern;

            amrex::ParallelFor(
                bx, Q, [=] AMREX_GPU_DEVICE(int i, int j, int k, int q) {
                    Real value = feqQian(rho0, {AMREX_D_DECL(0.0, 0.0, 0.0)}, q);
                    if (seed_pattern) {
                        const int code = (13 * i + 7 * j + 3 * k + 5 * q) % 97;
                        const Real perturbation = Real(1.0e-4) * Real(code - 48);
                        value = w[q] * (Real(1.0) + perturbation);
                    }
                    dst(i, j, k, q) = value;
                });
        }

        if (osiReferenceEnabled()) {
            // Oracle 与 OSI 从完全相同的 logical valid 初值出发；ghost 由首次通信填充。
            amrex::MultiFab::Copy(f_old.at(lev), state, 0, 0, Q, 0);
            amrex::MultiFab::Copy(f_new.at(lev), state, 0, 0, Q, 0);
        }
        // 这里只初始化 valid。首次碰撞所需 ghost 由 main/Cycle2 通过
        // FillGhostLevel 建立，避免把 ghost 生命周期混入网格构造回调。
    }
}

void AmrCoreLBM::ErrorEst(int lev, amrex::TagBoxArray& tags, amrex::Real time, int ngrow) {
    // 受控回归专用：依次新建、平移、清除加密区，确保实际走过布局生命周期。
    // 默认关闭；不参与物理算例的加密判据，也不保存独立时间状态。
    if (probe_.regrid_fixture) {
        const int coarse_step = static_cast<int>(std::llround(time / grid_.dt));
        const int epoch = (coarse_step + 1) / 32;
        const bool refine = epoch > 0 && epoch != 5;
        const Box domain = Geom(lev).Domain();
        const auto lower = amrex::lbound(domain);
        const auto size = amrex::length(domain);
        const Real shift = (epoch == 4 || epoch >= 7) ? Real(0.0625) : Real(0);
        const Real inset = Real(0.25) + Real(0.05) * lev;
        for (MFIter mfi(tags); mfi.isValid(); ++mfi) {
            const auto tag = tags.array(mfi);
            amrex::ParallelFor(mfi.validbox(),
                               [=] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {
                                   const Real x = Real(i - lower.x) / size.x;
                                   const Real y = Real(j - lower.y) / size.y;
                                   bool inside = refine && x >= inset + shift && x < Real(1) - inset + shift &&
                                                 y >= inset && y < Real(1) - inset;
#if AMREX_SPACEDIM == 3
                                   const Real z = Real(k - lower.z) / size.z;
                                   inside = inside && z >= inset && z < Real(1) - inset;
#endif
                                   tag(i, j, k) = inside ? TagBox::SET : TagBox::CLEAR;
                               });
        }
        return;
    }

    if (lev >= err.size()) {
        return;
    }

    // InitMesh() invokes ErrorEst() before the first runtime RefineMesh().
    if (regrid_tag_counts.size() != static_cast<std::size_t>(max_level + 1)) {
        regrid_tag_counts.assign(max_level + 1, -1);
    }

    ComputeMacroLevel(lev);
    FillMacroGhostLevel(lev, time);
    ComputeVorticityLevel(lev); // 计算vort比较慢

    const int tagval = TagBox::SET;
    const int clearval = TagBox::CLEAR;

    const MultiFab& level_layout =
        streamMode() == 1 ? osi_state.at(lev) : f_old.at(lev);
    const MultiFab& vort_lev = vorticity[lev];

    amrex::IntVect lo2 = static_lo[lev + max_ref_level + 1];
    amrex::IntVect hi2 = static_hi[lev + max_ref_level + 1];

    const auto geomdata = geom[lev].data();
    amrex::Gpu::DeviceVector<amrex::RealVect> points_d = convertToDeviceVector(points);

    for (MFIter mfi(level_layout, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const Box& bx = mfi.growntilebox(0);
        const auto vort = vort_lev.array(mfi);
        const auto tagfab = tags.array(mfi);

        const IntVect& lo = bx.smallEnd();
        const IntVect& hi = bx.bigEnd();

        Real err_value = err[lev];
        RealVect* points_p = points_d.data();
        const int points_num = particle_num;

        amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
#if AMREX_SPACEDIM == 3
            // state_error_6(i, j, k, tagfab, vort, err_value, tagval, clearval, lev, geomdata, lo2, hi2, points_p, points_num);
            state_error_3(i, j, k, tagfab, vort, err_value, tagval, clearval, lev, geomdata, lo2, hi2, points_p, points_num);
#else
            state_error_3(i, j, k, tagfab, vort, err_value, tagval, clearval, lev, geomdata, lo2, hi2, points_p, points_num);
#endif
            // state_error_2(i, j, k, tagfab, vort, err_value, tagval, clearval, lev, geomdata, lo2, hi2, pos);
            // state_error_4(i, j, k, tagfab, vort, err_value, tagval, clearval, lev, geomdata, lo2, hi2, points_p, points_num);
            // state_error_5(i, j, k, tagfab, vort, err_value, tagval, clearval, lev, geomdata, lo2, hi2, points_p, points_num);
            // state_error_6(i, j, k, tagfab, vort, err_value, tagval, clearval, lev, geomdata, lo2, hi2, points_p, points_num);
        });
    }

    amrex::ReduceOps<amrex::ReduceOpSum> reduce_op;
    amrex::ReduceData<amrex::Long> reduce_data(reduce_op);
    using ReduceTuple = amrex::GpuTuple<amrex::Long>;
    for (MFIter mfi(tags, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const Box& bx = mfi.tilebox();
        const auto tagfab = tags.const_array(mfi);
        reduce_op.eval(bx, reduce_data,
                       [=] AMREX_GPU_DEVICE(int i, int j, int k) -> ReduceTuple {
                           return {amrex::Long(tagfab(i, j, k) == TagBox::SET)};
                       });
    }
    amrex::Long tag_count = amrex::get<0>(reduce_data.value());
    ParallelDescriptor::ReduceLongSum(tag_count);
    regrid_tag_counts[lev] = tag_count;
    bool refinement_diagnostics = false;
    ParmParse("verification").query("refinement_diagnostics", refinement_diagnostics);
    if (refinement_diagnostics) {
        PrintRefinementCriteriaDiagnostics(lev, time);
    }
}
