#ifndef BOX3D_OSI_INTERPOLATION_COVERAGE_H_
#define BOX3D_OSI_INTERPOLATION_COVERAGE_H_

#include <AMReX_BoxArray.H>
#include <AMReX_BoxList.H>
#include <AMReX_Geometry.H>

#include <algorithm>
#include <sstream>

namespace Box3dDetail {

inline amrex::BoxList MissingInterpolationSources(
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

inline void RequireInterpolationCoverage(
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

} // 覆盖检查命名空间

#endif
