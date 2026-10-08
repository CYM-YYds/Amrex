#include "AmrCoreLBM.H"
#include "AmrCoreLBM_detail.H"
#include "InterpolationCoverage.H"

#include <AMReX_BoxList.H>
#include <AMReX_MultiFabUtil.H>
#include <AMReX_ParIter.H>
#include <AMReX_PhysBCFunct.H>
#include <AMReX_Utility.H>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "Kernels.H"

using namespace amrex;
using namespace Box3dDetail;

// 网格生命周期与通用粗细层传输。

namespace {
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

}

void AmrCoreLBM::InitMesh(amrex::Real cur_time) {
    InitFromScratch(cur_time);
    RebuildCoarseFineCaches();
}

void AmrCoreLBM::FillCoarsePatch(int lev, amrex::Real time, amrex::MultiFab& mf) // 根本没有用到
{

    Interpolater* mapper = &cell_cons_interp;

    if (Gpu::inLaunchRegion()) {
        GpuBndryFuncFab<AmrCoreFill> gpu_bndry_func(AmrCoreFill{});
        PhysBCFunct<GpuBndryFuncFab<AmrCoreFill>> cphysbc(geom[lev - 1], bcs, gpu_bndry_func);
        PhysBCFunct<GpuBndryFuncFab<AmrCoreFill>> fphysbc(geom[lev], bcs, gpu_bndry_func);

        amrex::InterpFromCoarseLevel(mf, time, f_old[lev - 1], 0, 0, Q, geom[lev - 1], geom[lev],
                                     cphysbc, 0, fphysbc, 0, refRatio(lev - 1),
                                     mapper, bcs, 0);
    } else {
        CpuBndryFuncFab bndry_func(nullptr); // Without EXT_DIR, we can pass a nullptr.
        PhysBCFunct<CpuBndryFuncFab> cphysbc(geom[lev - 1], bcs, bndry_func);
        PhysBCFunct<CpuBndryFuncFab> fphysbc(geom[lev], bcs, bndry_func);

        amrex::InterpFromCoarseLevel(mf, time, f_old[lev - 1], 0, 0, Q, geom[lev - 1], geom[lev],
                                     cphysbc, 0, fphysbc, 0, refRatio(lev - 1),
                                     mapper, bcs, 0);
    }
}

void AmrCoreLBM::FillPatch(int lev, amrex::Real time, amrex::MultiFab& mf) {

    Interpolater* mapper = &cell_cons_interp;

    if (lev == 0) {
        amrex::MultiFab& f_old_lev = f_old[lev];
        amrex::Vector<amrex::MultiFab*> cmf{&f_old_lev};
        amrex::Vector<Real> ctime{time};

        if (Gpu::inLaunchRegion()) {
            GpuBndryFuncFab<AmrCoreFill> gpu_bndry_func(AmrCoreFill{});
            PhysBCFunct<GpuBndryFuncFab<AmrCoreFill>> physbc(geom[lev], bcs, gpu_bndry_func);
            FillPatchSingleLevel(mf, time, cmf, ctime, 0, 0, Q, geom[lev], physbc, 0);
        } else {
            CpuBndryFuncFab bndry_func(nullptr);
            PhysBCFunct<CpuBndryFuncFab> physbc(geom[lev], bcs, bndry_func);
            FillPatchSingleLevel(mf, time, cmf, ctime, 0, 0, Q, geom[lev], physbc, 0);
        }
    } else {
        amrex::MultiFab& f_old_lev_c = f_old[lev - 1];
        amrex::MultiFab& f_old_lev_f = f_old[lev];

        amrex::Vector<amrex::MultiFab*> cmf{&f_old_lev_c};
        amrex::Vector<amrex::MultiFab*> fmf{&f_old_lev_f};

        amrex::Vector<Real> ctime{time};
        amrex::Vector<Real> ftime{time};

        if (Gpu::inLaunchRegion()) {
            GpuBndryFuncFab<AmrCoreFill> gpu_bndry_func(AmrCoreFill{});
            PhysBCFunct<GpuBndryFuncFab<AmrCoreFill>> cphysbc(geom[lev - 1], bcs, gpu_bndry_func);
            PhysBCFunct<GpuBndryFuncFab<AmrCoreFill>> fphysbc(geom[lev], bcs, gpu_bndry_func);
            amrex::FillPatchTwoLevels(mf, time, cmf, ctime, fmf, ftime, 0, 0, Q,
                                      geom[lev - 1], geom[lev], cphysbc, 0, fphysbc, 0,
                                      refRatio(lev - 1), mapper, bcs, 0);
        } else {
            CpuBndryFuncFab bndry_func(nullptr);
            PhysBCFunct<CpuBndryFuncFab> cphysbc(geom[lev - 1], bcs, bndry_func);
            PhysBCFunct<CpuBndryFuncFab> fphysbc(geom[lev], bcs, bndry_func);

            amrex::FillPatchTwoLevels(mf, time, cmf, ctime, fmf, ftime, 0, 0, Q,
                                      geom[lev - 1], geom[lev], cphysbc, 0, fphysbc, 0,
                                      refRatio(lev - 1), mapper, bcs, 0);
        }
    }
}

amrex::Interpolater* AmrCoreLBM::DdfInterpolater() const {
    if (interp_mode == 1) {
        return &cell_cons_interp;
    }
    if (interp_mode == 2) {
        return &quadratic_interp;
    }
    return &cell_bilinear_interp;
}

void AmrCoreLBM::BuildDirectInterpolationCache(int lev) {
    const double start = amrex::second();
    AMREX_ALWAYS_ASSERT(lev > 0 && lev <= max_level);

    const MultiFab& fine_layout =
        stream_mode == 1 ? osi_state.at(lev) : f_old.at(lev);
    const auto fill_ng = fine_layout.nGrowVect();
    Interpolater* interp_mapper = DdfInterpolater();
    const auto coarsener =
        interp_mapper->BoxCoarsener(refRatio(lev - 1));
    const BoxArray& fine_ba = fine_layout.boxArray();
    const BoxArray fine_ba_simplified = fine_ba.simplified();
    const MultiFab& coarse_layout =
        stream_mode == 1 ? osi_state.at(lev - 1) : f_old.at(lev - 1);
    const BoxArray coarse_valid = coarse_layout.boxArray().simplified();
    Box fine_domain = Geom(lev).Domain();
    for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
        if (Geom(lev).isPeriodic(dir)) {
            fine_domain.grow(dir, fill_ng[dir]); // 根据周期性条件扩展 fine_domain 的边界。
        }
    }

    Vector<Box> coarse_boxes;
    Vector<int> coarse_owners;
    auto& coarse_stage = interp_direct_coarse_stage[lev];
    auto& fine_work_boxes = interp_direct_fine_boxes[lev];
    auto& fine_indices = interp_direct_fine_index[lev];
    auto& needs_physical_fill = interp_direct_needs_physical_fill[lev];
    auto& osi_decode_regions = osi_interp_decode_boxes[lev];
    coarse_stage.clear();
    fine_work_boxes.clear();
    fine_indices.clear();
    needs_physical_fill.clear();
    osi_decode_regions.clear();
    osi_interp_local_copy_tags.at(lev).undefine();

    for (int fine_index = 0; fine_index < fine_ba.size(); ++fine_index) {
        const Box target =
            amrex::grow(fine_ba[fine_index], fill_ng) & fine_domain;
        const BoxList leftover =
            fine_ba_simplified.complementIn(target, Geom(lev).periodicity()); // 计算 target 中没有被 fine_ba_simplified 覆盖的区域, 并以多个互不重叠的 Box 返回，考虑周期性情况。
        for (const Box& work_box : leftover) {
            const Box coarse_box = coarsener.doit(work_box);
            // 缓存建立时检查完整模板来源，避免后续复制留下域内 staging 缺口。
            RequireInterpolationCoverage(
                coarse_box, coarse_valid, Geom(lev - 1), lev, fine_index,
                work_box, interp_mode, fill_ng, nProper());
            const int owner = fine_layout.DistributionMap()[fine_index];
            bool needs_fill = false;
            for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) { // 检查任意方向的 coarse box 是否出现越界行为
                needs_fill =
                    needs_fill ||
                    (!Geom(lev - 1).isPeriodic(dir) &&
                     (coarse_box.smallEnd(dir) <
                          Geom(lev - 1).Domain().smallEnd(dir) ||
                      coarse_box.bigEnd(dir) >
                          Geom(lev - 1).Domain().bigEnd(dir)));
            }

            // 在当前 BOX3D 的 coarse/fine 对齐条件下，不同合法 work_box
            // 不会映射到同一个 coarse stencil；每个 work_box 独立建立
            // 一个 staging Fab，避免保留无效的全局去重搜索。
            coarse_boxes.push_back(coarse_box);
            coarse_owners.push_back(owner);
            fine_work_boxes.push_back(work_box);
            fine_indices.push_back(fine_index);
            needs_physical_fill.push_back(
                static_cast<unsigned char>(needs_fill));
        }
    }

    if (!coarse_boxes.empty()) {
        BoxArray coarse_stage_ba(
            coarse_boxes.data(), static_cast<int>(coarse_boxes.size()));
        DistributionMapping coarse_stage_dm(std::move(coarse_owners));
        coarse_stage.define(coarse_stage_ba, coarse_stage_dm, Q, 0);
    }
    if (osi_parallel_copy && ParallelDescriptor::NProcs() == 1 &&
        !coarse_boxes.empty()) {
        // 复用 AMReX ParallelCopy 的 CPC 本地 tag，只替换数据地址为 OSI raw 映射。
        Vector<OSI::LocalCopyTag> direct_tags;
        const auto& source_state = osi_state.at(lev - 1);
        const auto& cpc = coarse_stage.getCPC(
            IntVect(0), source_state, IntVect(0),
            Geom(lev - 1).periodicity()); // getCPC() 根据源、目标的 Box 布局和周期性，生成“目标区域该从哪个源区域取数据”的标签。
        AMREX_ALWAYS_ASSERT(cpc.m_LocTags);
        direct_tags.reserve(cpc.m_LocTags->size());
        for (const auto& tag : *cpc.m_LocTags) {
            const Box source_ring = amrex::grow(
                source_state.boxArray()[tag.srcIndex],
                source_state.nGrowVect());
            const Box destination_box = coarse_stage.boxArray()[tag.dstIndex];
            const auto source_lo = source_ring.smallEnd();
            const auto destination_lo = destination_box.smallEnd();
            direct_tags.push_back({source_state.const_array(tag.srcIndex),
                                   coarse_stage.array(tag.dstIndex),
                                   tag.sbox,
                                   tag.dbox,
                                   {{source_lo[0], source_lo[1], source_lo[2]},
                                    {source_ring.length(0), source_ring.length(1),
                                     source_ring.length(2)}},
                                   {{destination_lo[0], destination_lo[1], destination_lo[2]},
                                    {destination_box.length(0), destination_box.length(1),
                                     destination_box.length(2)}}});
        }
        osi_interp_local_copy_tags.at(lev).define(direct_tags);
    }
    // ParallelCopy 只从与 coarse_stage 在周期映射后对应相交的 coarse valid 区域读取数据。
    // 在这里一次性反查这些 source boxes，避免每个时间步解码整层 valid。
    if (stream_mode == 1) {
        const BoxArray& source_ba = osi_state.at(lev - 1).boxArray();
        const auto shifts =
            Geom(lev - 1).periodicity().shiftIntVect();
        Vector<BoxList> decode_candidates(source_ba.size());

        for (const Box& destination : coarse_boxes) {
            for (const IntVect& shift : shifts) {
                const Box source_query = destination - shift;
                for (const auto& [source_index, source_box] :
                     source_ba.intersections(source_query)) { // 求source_ba中的相交区域
                    decode_candidates[source_index].push_back(source_box);
                }
            }
        }

        osi_decode_regions.resize(source_ba.size());
        for (int source_index = 0; source_index < source_ba.size();
             ++source_index) {
            BoxList disjoint =
                amrex::removeOverlap(decode_candidates[source_index]);
            disjoint.simplify(true);
            AMREX_ALWAYS_ASSERT(disjoint.isDisjoint());
            osi_decode_regions[source_index].assign(
                disjoint.begin(), disjoint.end());
        }
    }

    interp_direct_cache_ready[lev] = 1;
    ++perf_stats.interp_cache_builds;
    perf_stats.interp_cache_build += amrex::second() - start;
}

void AmrCoreLBM::ApplyPhysicalBoundaryToInterpolationStage(int lev) {
    const int fine_lev = lev + 1;
    AMREX_ALWAYS_ASSERT(lev >= 0 && fine_lev <= finest_level);
    auto& coarse_stage = interp_direct_coarse_stage.at(fine_lev);
    const Box coarse_domain = Geom(lev).Domain();
    const auto coarse_lo = amrex::lbound(coarse_domain);
    const auto coarse_hi = amrex::ubound(coarse_domain);
    const auto coarse_periodic = Geom(lev).isPeriodicArray();

    // coarse_stage 的域外 stencil 沿最近的非周期域内单元延拓；周期方向
    // 已由调用者的 ParallelCopy(periodicity) 完成映射。
    for (MFIter mfi(coarse_stage, false); mfi.isValid(); ++mfi) {
        const int stage_index = mfi.index();
        if (!interp_direct_needs_physical_fill.at(fine_lev).at(stage_index)) {
            continue;
        }
        const Box stage_box = mfi.validbox();
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
                const int src_k = coarse_periodic[2]
                                      ? k
                                      : amrex::max(coarse_lo.z,
                                                   amrex::min(k, coarse_hi.z));
                if (src_i != i || src_j != j || src_k != k) {
                    coarse(i, j, k, q) = coarse(src_i, src_j, src_k, q);
                }
            });
    }
}

void AmrCoreLBM::FillDdfGhostFromCoarse(
    int lev, amrex::Real time, bool apply_scale) {
    (void)time;
    const int fine_lev = lev + 1;
    AMREX_ALWAYS_ASSERT(lev >= 0 && fine_lev <= finest_level);
    AMREX_ALWAYS_ASSERT(refRatio(lev) == IntVect(2));

    auto& fine_state = f_old.at(fine_lev);
    const auto& coarse_state = f_old.at(lev);
    auto& coarse_stage = interp_direct_coarse_stage.at(fine_lev);
    const auto& fine_work_boxes = interp_direct_fine_boxes.at(fine_lev);
    const auto& fine_indices = interp_direct_fine_index.at(fine_lev);
    const Real scale = tau.at(fine_lev) / tau.at(lev) / Real(2.0);

    if (fine_indices.empty()) {
        return;
    }

    ScopedPerfTimer timer(perf_stats.interp_fillpatch);
    const int interp_mode_local = interp_mode;

    // 阶段 1：把 canonical coarse valid/周期像装入稀疏 stencil。
    coarse_stage.ParallelCopy(
        coarse_state, 0, 0, Q, IntVect(0), IntVect(0),
        Geom(lev).periodicity());
    // 阶段 2：两条插值路径共享相同的物理边界规则。
    ApplyPhysicalBoundaryToInterpolationStage(lev);

    // 阶段 3/4：缩放当前 stencil，随后写入 canonical fine 布局。
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
        const Box fine_box = fine_work_boxes.at(stage_index);
        const auto fine = fine_state.array(fine_index);
        if (interp_mode_local == 0) {
            amrex::ParallelFor(
                fine_box, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                    interp_bilinear_d3q(i, j, k, fine, coarse);
                });
        } else if (interp_mode_local == 1) {
            const Box coarse_parent_box = amrex::coarsen(fine_box, 2);
            amrex::ParallelFor(
                coarse_parent_box,
                [=] AMREX_GPU_DEVICE(int ic, int jc, int kc) {
                    interp_cell_cons_linear_children_d3q(
                        ic, jc, kc, fine, coarse, fine_box);
                });
        } else {
            const Box coarse_parent_box = amrex::coarsen(fine_box, 2);
            amrex::ParallelFor(
                coarse_parent_box,
                [=] AMREX_GPU_DEVICE(int ic, int jc, int kc) {
                    interp_cell_quadratic_children_d3q(
                        ic, jc, kc, fine, coarse, fine_box);
                });
        }
    }
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
                            interp_bilinear_d3q(i, j, k, fine, coarse);
                        });
                } else if (interp_mode_local == 1) {
                    const Box coarse_parent_box = amrex::coarsen(bx, 2);
                    amrex::ParallelFor(
                        coarse_parent_box,
                        [=] AMREX_GPU_DEVICE(int ic, int jc, int kc) {
                            interp_cell_cons_linear_children_d3q(
                                ic, jc, kc, fine, coarse, bx);
                        });
                } else {
                    const Box coarse_parent_box = amrex::coarsen(bx, 2);
                    amrex::ParallelFor(
                        coarse_parent_box,
                        [=] AMREX_GPU_DEVICE(int ic, int jc, int kc) {
                            interp_cell_quadratic_children_d3q(
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
    regrid_tag_counts.assign(max_level + 1, -1);
    for (auto& buffer : average_interface_buffer) {
        buffer.clear();
    }
    for (auto& buffer : interp_direct_coarse_stage) {
        buffer.clear();
    }
    for (auto& boxes : interp_direct_fine_boxes) {
        boxes.clear();
    }
    for (auto& indices : interp_direct_fine_index) {
        indices.clear();
    }
    for (auto& flags : interp_direct_needs_physical_fill) {
        flags.clear();
    }
    for (auto& boxes : osi_interp_decode_boxes) {
        boxes.clear();
    }
    std::fill(
        interp_direct_cache_ready.begin(),
        interp_direct_cache_ready.end(), 0);
    regrid(0, cur_time);
    RebuildCoarseFineCaches();
    bool regrid_valid_check = false;
    ParmParse("verification").query("regrid_valid_check", regrid_valid_check);
    if (stream_mode == 1 && (osiReferenceEnabled() || regrid_valid_check)) {
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

void AmrCoreLBM::RebuildCoarseFineCaches() {
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
        interp_direct_coarse_stage[lev].clear();
        interp_direct_fine_boxes[lev].clear();
        interp_direct_fine_index[lev].clear();
        interp_direct_needs_physical_fill[lev].clear();
        osi_interp_decode_boxes[lev].clear();
        interp_direct_cache_ready[lev] = 0;
        boundary_work_boxes[lev].clear();
        average_interface_buffer[lev].clear();
        average_interface_fine_box[lev].clear();
    }

    // 构造 coarse 覆盖掩码和 coarse-fine 交界掩码，供粗层
    // Collide/Stream 跳过被细层替代的区域，并供交界限制操作使用。
    RebuildCoarseFineMasks();

    // 预先找出各 fine Fab 位于非周期物理边界上的互不重叠工作 Box，
    // Boundary() 可直接遍历这些 Box，而不必在每个时间步重复分析几何关系。
    BuildBoundaryWorkBoxes();

    // 为每个 fine ghost work_box 建立所需的 coarse stencil staging Fab、
    // fine Fab 索引和 owner 映射，供 FillDdfGhostFromCoarse() 直接插值。
    // stencil 大小由当前 interp_mode 对应的 Interpolater::BoxCoarsener 决定。
    BuildInterpolationCache();

    // 建立 fine-to-coarse 平均下传缓存：全覆盖模式使用完整 coarse 缓冲区，
    // 稀疏模式只保存 coarse-fine 交界条带及其对应的 fine Fab 索引。
    BuildAverageCache();
}

void AmrCoreLBM::RebuildCoarseFineMasks() {
    if (stream_mode == 1) {
        RebuildCoarseFineMasksForState(osi_state);
    } else {
        RebuildCoarseFineMasksForState(f_old);
    }
}

void AmrCoreLBM::RebuildCoarseFineMasksForState(
    const Vector<MultiFab>& state) {
    if (cf_mask_mode == 0) {
        return;
    }

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

                for (int dk = -1; dk <= 1; ++dk) {
                    for (int dj = -1; dj <= 1; ++dj) {
                        for (int di = -1; di <= 1; ++di) {
                            const int ni = i + di;
                            const int nj = j + dj;
                            const int nk = k + dk;
                            // 非周期物理域外 ghost 的 covered 值为 0，故被
                            // fine 覆盖且贴近物理边界的 coarse 单元也属 interface。
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

void AmrCoreLBM::BuildBoundaryWorkBoxes() {
    for (int lev = 0; lev <= finest_level; ++lev) {
        const BoxArray& ba = boxArray(lev);
        const Box domain = Geom(lev).Domain();
        const auto is_periodic = Geom(lev).isPeriodicArray();
        auto& level_boundary_boxes = boundary_work_boxes[lev];
        level_boundary_boxes.resize(ba.size());

        for (int ibox = 0; ibox < ba.size(); ++ibox) {
            const Box& valid_box = ba[ibox];

            // 边界条件同样会更新域内Fab对象中、紧邻非周期物理边界的ghost单元。
            const Box boundary_source_box = amrex::grow(valid_box, nghost) & domain;
            BoxList boundary_faces;

            for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
                if (is_periodic[dir]) {
                    continue;
                }

                const int lo = domain.smallEnd(dir);
                const int hi = domain.bigEnd(dir);
                if (valid_box.smallEnd(dir) == lo) {
                    Box face = boundary_source_box;
                    face.setSmall(dir, lo);
                    face.setBig(dir, std::min(hi, lo + nghost));
                    boundary_faces.push_back(face);
                }
                if (hi != lo && valid_box.bigEnd(dir) == hi) {
                    Box face = boundary_source_box;
                    face.setSmall(dir, std::max(lo, hi - nghost));
                    face.setBig(dir, hi);
                    boundary_faces.push_back(face);
                }
            }

            const BoxList disjoint_faces = amrex::removeOverlap(boundary_faces);
            level_boundary_boxes[ibox].assign(disjoint_faces.begin(), disjoint_faces.end());
        }
    }
}

void AmrCoreLBM::BuildInterpolationCache() {
    for (int lev = 1; lev <= finest_level; ++lev) {
        BuildDirectInterpolationCache(lev);
    }
}

void AmrCoreLBM::BuildAverageCache() {
    for (int lev = 0; lev < finest_level; ++lev) {
        const MultiFab& fine_layout =
            stream_mode == 1 ? osi_state.at(lev + 1) : f_old.at(lev + 1);
        const BoxArray coarse_from_fine =
            amrex::coarsen(fine_layout.boxArray(), refRatio(lev));

        BoxList interface_boxes;
        Vector<int> interface_owners;
        Vector<int> fine_box_indices;
        const auto& periodicity = Geom(lev).periodicity();
        const auto& fine_dm = fine_layout.DistributionMap();
        for (int ibox = 0; ibox < coarse_from_fine.size(); ++ibox) {
            const Box& covered_box = coarse_from_fine[ibox];
            // 保留非周期物理域外的一圈未覆盖单元，与 interface_mask 的判定一致。
            const Box search_box = amrex::grow(covered_box, 1);
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
            average_interface_buffer[lev].define(interface_ba, interface_dm, Q, 0);
            average_interface_fine_box[lev] = std::move(fine_box_indices); // “把 fine_box_indices 这份索引列表转交给 average_interface_fine_box[lev]，避免复制，直接拿走内部数据。”
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

    if (stream_mode == 0) {
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
            const MultiFab& coarse = f_old.at(lev - 1);
            MultiFab coarse_canonical(coarse.boxArray(),
                                      coarse.DistributionMap(), Q, nghost);
            const Real scale = tau.at(lev) / tau.at(lev - 1) / Real(2.0);
            MultiFab::Copy(coarse_canonical, coarse, 0, 0, Q, 0);
            ScaleCanonical(density.at(lev - 1), velocity.at(lev - 1),
                                coarse_canonical, scale);
            InterpolateCanonicalToFine(
                lev, time, reference, coarse_canonical);
        }
    }

    force_lev.setVal(0.0, nghost);
    shear_lev.setVal(0.0, nghost);
    vort_lev.setVal(0.0, nghost);
}

void AmrCoreLBM::RemakeLevel(int lev, amrex::Real time, const amrex::BoxArray& ba,
                             const amrex::DistributionMapping& dm) { // 某个已存在的细层网格布局发生变化后，按新的 ba/dm 重建该层，并把旧流场尽可能迁移过去。主要由RefineMesh()调用
    amrex::MultiFab u_new(ba, dm, AMREX_SPACEDIM, nghost);           // 按新 ba/dm 重建派生宏观场；density/velocity 后续由 DDF 重新计算。
    amrex::MultiFab rho_new(ba, dm, 1, nghost);
    amrex::MultiFab vort_new(ba, dm, 2, nghost);
    amrex::MultiFab force_new(ba, dm, AMREX_SPACEDIM, nghost);
    amrex::MultiFab shear_new(ba, dm, 1, nghost);

    if (stream_mode == 1) {
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
            InterpolateOsiFinePatchFromCoarse(
                lev, time, fpc.ba_fine_patch, fpc.dm_patch,
                new_osi_state);
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
    osi_state.at(lev).define(ba, dm, Q, nghost);
    osi_phase.at(lev) = 0;
    BuildOsiCommunicationRegionCache(lev);
}

void AmrCoreLBM::InitializeNewLevelFromCoarse(int lev, Real time) {
    AMREX_ALWAYS_ASSERT(lev > 0);
    const bool use_osi = stream_mode == 1;
    if (use_osi) {
        AMREX_ALWAYS_ASSERT(osi_phase.at(lev) == 0);
    }

    MultiFab& fine_state =
        use_osi ? osi_state.at(lev) : f_old.at(lev);
    const MultiFab& coarse_state =
        use_osi ? osi_state.at(lev - 1) : f_old.at(lev - 1);

    ComputeMacroLevel(lev - 1);
    const MultiFab& coarse_density = density.at(lev - 1);
    const MultiFab& coarse_velocity = velocity.at(lev - 1);
    const Real scale = tau.at(lev) / tau.at(lev - 1) / Real(2.0);

    // OSI direct 路径已经能够按 phase-aware raw 地址完成整层 Q 分量复制；
    // 这里不再把同一份 coarse 状态拆成多个 batch。Copy 只负责数据搬运，
    // 缩放和粗到细插值仍由后续两个阶段完成。
    if (use_osi && osi_parallel_copy) {
        MultiFab coarse_canonical(
            coarse_state.boxArray(), coarse_state.DistributionMap(), Q,
            nghost);
        ParallelCopyOsi(lev - 1, coarse_state, osi_phase.at(lev - 1),
                        coarse_canonical, 0);
        ScaleCanonical(coarse_density, coarse_velocity,
                            coarse_canonical, scale);
        InterpolateCanonicalToFine(
            lev, time, fine_state, coarse_canonical);
        return;
    }

    MultiFab coarse_canonical(coarse_state.boxArray(),
                              coarse_state.DistributionMap(), Q, nghost);
    if (use_osi) {
        DecodeOsiValid(coarse_state, osi_phase.at(lev - 1),
                            coarse_canonical);
    } else {
        MultiFab::Copy(coarse_canonical, coarse_state, 0, 0, Q, 0);
    }
    ScaleCanonical(coarse_density, coarse_velocity,
                        coarse_canonical, scale);
    InterpolateCanonicalToFine(
        lev, time, fine_state, coarse_canonical);
}

void AmrCoreLBM::InterpolateCanonicalToFine(
    int lev, Real time, MultiFab& fine, MultiFab& coarse_canonical) {
    if (Gpu::inLaunchRegion()) {
        GpuBndryFuncFab<AmrCoreFill> gpu_bndry_func(AmrCoreFill{});
        PhysBCFunct<GpuBndryFuncFab<AmrCoreFill>> cphysbc(
            geom[lev - 1], bcs, gpu_bndry_func);
        PhysBCFunct<GpuBndryFuncFab<AmrCoreFill>> fphysbc(
            geom[lev], bcs, gpu_bndry_func);
        amrex::InterpFromCoarseLevel(
            fine, time, coarse_canonical, 0, 0, Q,
            geom[lev - 1], geom[lev], cphysbc, 0, fphysbc, 0,
            refRatio(lev - 1), DdfInterpolater(), bcs, 0);
    } else {
        CpuBndryFuncFab bndry_func(nullptr);
        PhysBCFunct<CpuBndryFuncFab> cphysbc(
            geom[lev - 1], bcs, bndry_func);
        PhysBCFunct<CpuBndryFuncFab> fphysbc(
            geom[lev], bcs, bndry_func);
        amrex::InterpFromCoarseLevel(
            fine, time, coarse_canonical, 0, 0, Q,
            geom[lev - 1], geom[lev], cphysbc, 0, fphysbc, 0,
            refRatio(lev - 1), DdfInterpolater(), bcs, 0);
    }
}

void AmrCoreLBM::ClearLevel(int lev) {
    osi_local_copy_tags[lev].undefine();
    osi_remote_copy_tags[lev].clear();
    osi_mpi_pack_tags[lev].undefine();
    osi_mpi_unpack_tags[lev].undefine();
    osi_mpi_send_offsets[lev].clear();
    osi_mpi_recv_offsets[lev].clear();
    osi_mpi_send_counts[lev].clear();
    osi_mpi_recv_counts[lev].clear();
    osi_mpi_send_device[lev].clear();
    osi_mpi_recv_device[lev].clear();
    osi_mpi_send_host[lev].clear();
    osi_mpi_recv_host[lev].clear();
    f_old[lev].clear();
    f_new[lev].clear();
    osi_state[lev].clear();
    osi_decode_boxes[lev].clear();
    osi_encode_boxes[lev].clear();
    osi_phase[lev] = 0;
    velocity[lev].clear();
    vorticity[lev].clear();
    density[lev].clear();
    shear[lev].clear();
    force[lev].clear();
}

void AmrCoreLBM::MakeNewLevelFromScratch(int lev, amrex::Real time, const amrex::BoxArray& ba,
                                         const amrex::DistributionMapping& dm) {
    // 作用“拿到已经规划好的某层网格后，，并从“初始物理条件”生成数据”,InitMesh()会调用到它
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
    if (stream_mode == 1) {
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

    if (stream_mode == 1) {
        amrex::MultiFab& state = osi_state[lev];
        state.setVal(std::numeric_limits<Real>::quiet_NaN());
        for (MFIter mfi(state, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
            const Box& bx = mfi.tilebox();
            const Array4<Real>& dst = state.array(mfi);
            const bool seed_pattern = osi_verification_pattern;

            amrex::ParallelFor(
                bx, Q, [=] AMREX_GPU_DEVICE(int i, int j, int k, int q) {
                    Real value = feqQian(rho0, {0.0, 0.0, 0.0}, q);
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
        stream_mode == 1 ? osi_state.at(lev) : f_old.at(lev);
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
            // state_error_2(i, j, k, tagfab, vort, err_value, tagval, clearval, lev, geomdata, lo2, hi2, pos);
            state_error_3(i, j, k, tagfab, vort, err_value, tagval, clearval, lev, geomdata, lo2, hi2, points_p, points_num);
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
}
