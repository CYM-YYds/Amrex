#include <AMReX_BoxList.H>
#include <AMReX_MFIter.H>
#include <AMReX_MultiFabUtil.H>
#include <AMReX_ParIter.H>
#include <AMReX_ParallelDescriptor.H>
#include <AMReX_ParmParse.H>
#include <AMReX_PhysBCFunct.H>
#include <AMReX_PlotFileUtil.H>
#include <AMReX_Reduce.H>
#include <AMReX_Utility.H>
#include <AMReX_VisMF.H>
#include <AMReX_Gpu.H>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>

#ifdef AMREX_MEM_PROFILING
#include <AMReX_MemProfiler.H>
#endif

#include "AmrCoreLBM.H"
#include "Kernels.H"
#include "InitParticles.H"

using namespace amrex;

namespace {
constexpr char checkpoint_label_v1[] = "LBMCheckpoint";
constexpr char checkpoint_label_v2[] = "LBMCheckpointV2";
constexpr char checkpoint_layout_ab[] = "canonical_ab_two_array_v1";
constexpr char checkpoint_layout_osi[] = "canonical_osi_single_array_v1";
class ScopedPerfTimer {
  public:
    explicit ScopedPerfTimer(double& accum)
        : m_accum(accum), m_start(amrex::second()) {}

    ~ScopedPerfTimer() {
        amrex::Gpu::streamSynchronize();
        m_accum += amrex::second() - m_start;
    }

  private:
    double& m_accum;
    double m_start;
};
} // namespace

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

//********************************************************************//
//                           constructor                              //
//********************************************************************//
AmrCoreLBM::AmrCoreLBM(amrex::Geometry const& level_0_geom, amrex::AmrInfo const& amr_info,
                       LbmGridParams grid)
    : AmrCore(level_0_geom, amr_info), grid_(grid) {
    ReadParameters();

    int nlevs_max = max_level + 1;

    f_new.resize(nlevs_max);
    f_old.resize(nlevs_max);
    osi_state.resize(nlevs_max);
    osi_phase.resize(nlevs_max, 0);
    osi_sync_buffer.resize(nlevs_max);
    osi_decode_boxes.resize(nlevs_max);
    osi_encode_boxes.resize(nlevs_max);
    osi_decode_tags.resize(nlevs_max);
    osi_encode_tags.resize(nlevs_max);
    average_interface_buffer.resize(nlevs_max);
    average_interface_fine_box.resize(nlevs_max);
    covered_mask.resize(nlevs_max);
    interface_mask.resize(nlevs_max);
    covered_cell_counts.resize(nlevs_max, 0);
    interface_cell_counts.resize(nlevs_max, 0);
    interp_direct_coarse_stage.resize(nlevs_max);
    interp_direct_fine_boxes.resize(nlevs_max);
    interp_direct_fine_index.resize(nlevs_max);
    interp_direct_needs_physical_fill.resize(nlevs_max);
    osi_interp_decode_boxes.resize(nlevs_max);
    interp_direct_cache_ready.resize(nlevs_max, 0);
    boundary_work_boxes.resize(nlevs_max);

    velocity.resize(nlevs_max);
    vorticity.resize(nlevs_max);
    shear.resize(nlevs_max);
    density.resize(nlevs_max);
    force.resize(nlevs_max);

    tau.resize(nlevs_max);
    tau[0] = grid_.viscosity / (cs2 * grid_.dt) + 0.5;

    for (int lev = 1; lev <= max_level; ++lev) {
        tau[lev] = 2 * (tau[lev - 1] - 0.5) + 0.5;
    }

    int bc_lo[] = {BCType::foextrap, BCType::foextrap, BCType::foextrap};
    int bc_hi[] = {BCType::foextrap, BCType::foextrap, BCType::foextrap};

    bcs.resize(Q);

    for (int idim = 0; idim < AMREX_SPACEDIM; idim++) { // 为每个 DDF 分量、每个空间方向设置低端和高端的边界类型,设置为foextrap
        for (int comp = 0; comp < Q; ++comp) {
            bcs[comp].setLo(idim, bc_lo[idim]);
            bcs[comp].setHi(idim, bc_hi[idim]);
        }
    }

    static_lo.resize(2 * nlevs_max);
    static_hi.resize(2 * nlevs_max);

    // 多颗粒生成
    particles.resize(particle_num);
    points.resize(particle_num);
    // generateSpheres(particle_num, R, NX, NY, NZ, points);
    // generateSpheres(D, NX, NY, NZ, points);
    points[0] = {0.5 * grid_.nx_cells, 0.5 * grid_.ny_cells,
                 0.5 * grid_.nz_cells};
    // points[1] = {X + 2.0*D, Y, Z};
    // points[0] = {X + 0.03*D, Y + 0.03*D, 21.00*D};
    // points[1] = {X - 0.03*D, Y - 0.03*D, 18.96*D};

    // 表面压力系数容器
    particlesCp.resize(particle_num);

    for (int lev = 0; lev <= max_level; lev++) {
        amrex::Real dx = Geom(lev).CellSizeArray()[0];

        for (int idim = 0; idim < AMREX_SPACEDIM; idim++) {
            static_lo[lev][idim] = 0 + (8); // 边界加密
            static_hi[lev][idim] = Geom(lev).Domain().length(idim) - (8);
        }

        // static_lo[lev+nlevs_max][0] = (center[0] + obj_lo_x - 0.2  * (obj_hi_x - obj_lo_x)) * dx_0 / dx;
        // static_lo[lev+nlevs_max][1] = (center[1] + obj_lo_y - 0.2  * (obj_hi_y - obj_lo_y)) * dx_0 / dx;
        // static_lo[lev+nlevs_max][2] = (center[2] + obj_lo_z - 0.0  * (obj_hi_z - obj_lo_z)) * dx_0 / dx;

        // static_hi[lev+nlevs_max][0] = (center[0] + obj_hi_x + 0.5  * (obj_hi_x - obj_lo_x)) * dx_0 / dx;
        // static_hi[lev+nlevs_max][1] = (center[1] + obj_hi_y + 0.2  * (obj_hi_y - obj_lo_y)) * dx_0 / dx;
        // static_hi[lev+nlevs_max][2] = (center[2] + obj_hi_z + 0.5  * (obj_hi_z - obj_lo_z)) * dx_0 / dx;

        // if(lev == 0)
        // {
        //     static_lo[lev+nlevs_max][0] = (center[0]- 1.5 * D) * dx_0 / dx;
        //     static_lo[lev+nlevs_max][1] = (center[1]- 2.0 * D) * dx_0 / dx;
        //     static_lo[lev+nlevs_max][2] = (center[2]- 2.0 * D) * dx_0 / dx;

        //     static_hi[lev+nlevs_max][0] = (center[0] + 5.0 * D) * dx_0 / dx;
        //     static_hi[lev+nlevs_max][1] = (center[1] + 2.0 * D) * dx_0 / dx;
        //     static_hi[lev+nlevs_max][2] = (center[2] + 2.0 * D) * dx_0 / dx;
        // }
        // if(lev == 1)
        // {
        //     static_lo[lev+nlevs_max][0] = (center[0]- 1.2 * D) * dx_0 / dx;
        //     static_lo[lev+nlevs_max][1] = (center[1]- 1.5 * D) * dx_0 / dx;
        //     static_lo[lev+nlevs_max][2] = (center[2]- 1.5 * D) * dx_0 / dx;

        //     static_hi[lev+nlevs_max][0] = (center[0] + 4.0 * D) * dx_0 / dx;
        //     static_hi[lev+nlevs_max][1] = (center[1] + 1.5 * D) * dx_0 / dx;
        //     static_hi[lev+nlevs_max][2] = (center[2] + 1.5 * D) * dx_0 / dx;
        // }
        // if(lev == 2)
        // {
        //     static_lo[lev+nlevs_max][0] = (center[0]- 1.0 * D) * dx_0 / dx;
        //     static_lo[lev+nlevs_max][1] = (center[1]- 1.0 * D) * dx_0 / dx;
        //     static_lo[lev+nlevs_max][2] = (center[2]- 1.0 * D) * dx_0 / dx;

        //     static_hi[lev+nlevs_max][0] = (center[0] + 3.0 * D) * dx_0 / dx;
        //     static_hi[lev+nlevs_max][1] = (center[1] + 1.0 * D) * dx_0 / dx;
        //     static_hi[lev+nlevs_max][2] = (center[2] + 1.0 * D) * dx_0 / dx;
        // }
        // if(lev == 3)
        // {
        //     static_lo[lev+nlevs_max][0] = (center[0]- 0.7 * D) * dx_0 / dx;
        //     static_lo[lev+nlevs_max][1] = (center[1]- 0.8 * D) * dx_0 / dx;
        //     static_lo[lev+nlevs_max][2] = (center[2]- 0.8 * D) * dx_0 / dx;

        //     static_hi[lev+nlevs_max][0] = (center[0] + 1.5 * D) * dx_0 / dx;
        //     static_hi[lev+nlevs_max][1] = (center[1] + 0.8 * D) * dx_0 / dx;
        //     static_hi[lev+nlevs_max][2] = (center[2] + 0.8 * D) * dx_0 / dx;
        // }
        // if(lev == 4)
        // {
        //     static_lo[lev+nlevs_max][0] = (center[0]- 2.0 * D) * dx_0 / dx;
        //     static_lo[lev+nlevs_max][1] = (center[1]- 2.0 * D) * dx_0 / dx;
        //     static_lo[lev+nlevs_max][2] = (center[2]- 2.0 * D) * dx_0 / dx;

        //     static_hi[lev+nlevs_max][0] = (center[0] + 4.0 * D) * dx_0 / dx;
        //     static_hi[lev+nlevs_max][1] = (center[1] + 2.0 * D) * dx_0 / dx;
        //     static_hi[lev+nlevs_max][2] = (center[2] + 2.0 * D) * dx_0 / dx;
        // }
    }
}

AmrCoreLBM::AmrCoreLBM() {
}
AmrCoreLBM::~AmrCoreLBM() = default;

//********************************************************************//
//                           help function                            //
//********************************************************************//
void AmrCoreLBM::PrintMeshInfo() {
    if (ParallelDescriptor::IOProcessor()) {
        amrex::Print() << "╔════════════════════════════════════════════════════════╗" << std::endl;
        amrex::Print() << "║               Mesh Information                         ║" << std::endl;
        amrex::Print() << "╚════════════════════════════════════════════════════════╝" << std::endl;

        printGridSummary(amrex::OutStream(), 0, finest_level);
        for (int i = 0; i <= finest_level; ++i) {
            amrex::Print() << std::setw(15) << std::left << "  blocking_factor[" << i << "]"
                           << std::setw(10) << std::right << blocking_factor[i] << std::endl;
        }
        for (int i = 0; i <= finest_level; ++i) {
            amrex::Print() << std::setw(15) << std::left << "  max_grid_size[" << i << "]  "
                           << std::setw(10) << std::right << max_grid_size[i] << std::endl;
        }
        for (int i = 0; i <= finest_level; ++i) {
            amrex::Print() << std::setw(15) << std::left << "  n_error_buf[" << i << "]    "
                           << std::setw(10) << std::right << n_error_buf[i] << std::endl;
        }
        amrex::Print() << std::setw(15) << std::left << "  grid_eff       " << std::setw(10) << std::right << grid_eff << std::endl;
        amrex::Print() << "╚════════════════════════════════════════════════════════╝" << std::endl;
        amrex::Print() << std::endl;
    }
}

void AmrCoreLBM::ResetPerfStats() {
    perf_stats = PerfStats{};
}

AmrCoreLBM::PerfStats const& AmrCoreLBM::GetPerfStats() const {
    return perf_stats;
}

double AmrCoreLBM::ComputeWeightedLatticeUpdatesPerCoarseStep() const {
    double weighted_updates = 0.0;

    for (int lev = 0; lev <= finest_level; ++lev) {
        double nlev = static_cast<double>(boxArray(lev).numPts()); // 获取当前层级总网格点数
        double nvalid = nlev;                                      // 设置有效点数

        if (lev < finest_level) {
            auto rr = refRatio(lev); // 获取细化比例
            long nc = 1;
            for (int idim = 0; idim < AMREX_SPACEDIM; ++idim) {
                nc *= rr[idim]; // 计算细化比例乘积 nc
            }

            double nfine = static_cast<double>(boxArray(lev + 1).numPts()); // 获取下一层级网格点数
            nvalid -= nfine / static_cast<double>(nc);                      // 更新有效点数
        }

        weighted_updates += static_cast<double>(1 << lev) * nvalid; // 计算加权更新 2^lev * nvalid
    }

    return weighted_updates;
}

void AmrCoreLBM::PrintLbmParm() {
    amrex::Print() << "╔════════════════════════════════════════════════════════╗" << std::endl;
    amrex::Print() << "║               LBM Parameters                           ║" << std::endl;
    amrex::Print() << "╚════════════════════════════════════════════════════════╝" << std::endl;

    amrex::Print() << std::setw(15) << std::left << "  NX     =" << std::setw(10) << std::right << Geom(0).Domain().length(0) << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  NY     =" << std::setw(10) << std::right << Geom(0).Domain().length(1) << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  NZ     =" << std::setw(10) << std::right << Geom(0).Domain().length(2) << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  dx_0   =" << std::setw(10) << std::right << grid_.dx << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  dx_min =" << std::setw(10) << std::right << grid_.dx_min << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  Re     =" << std::setw(10) << std::right << Re << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  cs2    =" << std::setw(10) << std::right << cs2 << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  p0     =" << std::setw(10) << std::right << p0 << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  Ma     =" << std::setw(10) << std::right << Ma << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  U0     =" << std::setw(10) << std::right << U0 << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  cf_mask=" << std::setw(10) << std::right << cf_mask_mode << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  col_mode=" << std::setw(10) << std::right << collide_mode << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  str_mode=" << std::setw(10) << std::right << stream_mode << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  int_mode=" << std::setw(10) << std::right << interp_mode << std::endl;

    for (int lev = 0; lev <= finest_level; lev++) {
        amrex::Print() << std::setw(15) << std::left << "  tau    =" << std::setw(10) << std::right << tau[lev] << std::endl;
    }

    amrex::Print() << "╚════════════════════════════════════════════════════════╝" << std::endl;
    amrex::Print() << std::endl;
}
void AmrCoreLBM::ReadParameters() {
    {
        ParmParse pp("lbm");
        pp.query("interp_mode", interp_mode);
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            interp_mode >= 0 && interp_mode <= 2,
            "lbm.interp_mode must be 0 (trilinear), 1 (GPU conservative "
            "linear), or 2 (GPU cell quadratic)");
    }
    {
        ParmParse pp("amr");
        pp.query("plot_file", plot_file);
        pp.query("grid_eff", grid_eff);
        pp.query("regrid_int", params_.regrid_int);
        pp.query("plot_int", params_.plot_int);
        pp.query("begin_plot", params_.begin_plot);

        // Read in the n_error_buf
        int cnt = pp.countval("n_error_buf");
        if (cnt > 0) {
            Vector<int> neb;
            pp.getarr("n_error_buf", neb);
            int n = std::min(cnt, max_level + 1);
            for (int i = 0; i < n; ++i) {
                n_error_buf[i] = IntVect(neb[i]);
            }
            for (int i = n; i <= max_level; ++i) {
                n_error_buf[i] = IntVect(neb[cnt - 1]);
            }
        }

        cnt = pp.countval("n_error_buf_x");
        if (cnt > 0) {
            int idim = 0;
            Vector<int> neb;
            pp.getarr("n_error_buf_x", neb);
            int n = std::min(cnt, max_level + 1);
            for (int i = 0; i < n; ++i) {
                n_error_buf[i][idim] = neb[i];
            }
            for (int i = n; i <= max_level; ++i) {
                n_error_buf[i][idim] = neb[n - 1];
            }
        }

#if (AMREX_SPACEDIM > 1)
        cnt = pp.countval("n_error_buf_y");
        if (cnt > 0) {
            int idim = 1;
            Vector<int> neb;
            pp.getarr("n_error_buf_y", neb);
            int n = std::min(cnt, max_level + 1);
            for (int i = 0; i < n; ++i) {
                n_error_buf[i][idim] = neb[i];
            }
            for (int i = n; i <= max_level; ++i) {
                n_error_buf[i][idim] = neb[n - 1];
            }
        }
#endif

#if (AMREX_SPACEDIM == 3)
        cnt = pp.countval("n_error_buf_z");
        if (cnt > 0) {
            int idim = 2;
            Vector<int> neb;
            pp.getarr("n_error_buf_z", neb);
            int n = std::min(cnt, max_level + 1);
            for (int i = 0; i < n; ++i) {
                n_error_buf[i][idim] = neb[i];
            }
            for (int i = n; i <= max_level; ++i) {
                n_error_buf[i][idim] = neb[n - 1];
            }
        }
#endif

        // Read in the max_grid_size
        cnt = pp.countval("max_grid_size");
        if (cnt > 0) {
            Vector<int> mgs;
            pp.getarr("max_grid_size", mgs);
            int last_mgs = mgs.back();
            mgs.resize(max_level + 1, last_mgs);
            SetMaxGridSize(mgs);
        }

        cnt = pp.countval("max_grid_size_x");
        if (cnt > 0) {
            int idim = 0;
            Vector<int> mgs;
            pp.getarr("max_grid_size_x", mgs);
            int n = std::min(cnt, max_level + 1);
            for (int i = 0; i < n; ++i) {
                max_grid_size[i][idim] = mgs[i];
            }
            for (int i = n; i <= max_level; ++i) {
                max_grid_size[i][idim] = mgs[n - 1];
            }
        }

#if (AMREX_SPACEDIM > 1)
        cnt = pp.countval("max_grid_size_y");
        if (cnt > 0) {
            int idim = 1;
            Vector<int> mgs;
            pp.getarr("max_grid_size_y", mgs);
            int n = std::min(cnt, max_level + 1);
            for (int i = 0; i < n; ++i) {
                max_grid_size[i][idim] = mgs[i];
            }
            for (int i = n; i <= max_level; ++i) {
                max_grid_size[i][idim] = mgs[n - 1];
            }
        }
#endif

#if (AMREX_SPACEDIM == 3)
        cnt = pp.countval("max_grid_size_z");
        if (cnt > 0) {
            int idim = 2;
            Vector<int> mgs;
            pp.getarr("max_grid_size_z", mgs);
            int n = std::min(cnt, max_level + 1);
            for (int i = 0; i < n; ++i) {
                max_grid_size[i][idim] = mgs[i];
            }
            for (int i = n; i <= max_level; ++i) {
                max_grid_size[i][idim] = mgs[n - 1];
            }
        }
#endif

        // Read in the blocking_factors
        cnt = pp.countval("blocking_factor");
        if (cnt > 0) {
            Vector<int> bf;
            pp.getarr("blocking_factor", bf);
            int last_bf = bf.back();
            bf.resize(max_level + 1, last_bf);
            SetBlockingFactor(bf);
        }

        cnt = pp.countval("blocking_factor_x");
        if (cnt > 0) {
            int idim = 0;
            Vector<int> bf;
            pp.getarr("blocking_factor_x", bf);
            int n = std::min(cnt, max_level + 1);
            for (int i = 0; i < n; ++i) {
                blocking_factor[i][idim] = bf[i];
            }
            for (int i = n; i <= max_level; ++i) {
                blocking_factor[i][idim] = bf[n - 1];
            }
        }

#if (AMREX_SPACEDIM > 1)
        cnt = pp.countval("blocking_factor_y");
        if (cnt > 0) {
            int idim = 1;
            Vector<int> bf;
            pp.getarr("blocking_factor_y", bf);
            int n = std::min(cnt, max_level + 1);
            for (int i = 0; i < n; ++i) {
                blocking_factor[i][idim] = bf[i];
            }
            for (int i = n; i <= max_level; ++i) {
                blocking_factor[i][idim] = bf[n - 1];
            }
        }
#endif

#if (AMREX_SPACEDIM == 3)
        cnt = pp.countval("blocking_factor_z");
        if (cnt > 0) {
            int idim = 2;
            Vector<int> bf;
            pp.getarr("blocking_factor_z", bf);
            int n = std::min(cnt, max_level + 1);
            for (int i = 0; i < n; ++i) {
                blocking_factor[i][idim] = bf[i];
            }
            for (int i = n; i <= max_level; ++i) {
                blocking_factor[i][idim] = bf[n - 1];
            }
        }
#endif
    }

    {
        ParmParse pp("lbm");
        pp.query("cf_mask_mode", cf_mask_mode);
        if (cf_mask_mode < 0 || cf_mask_mode > 1) {
            amrex::Abort("lbm.cf_mask_mode must be 0 or 1");
        }
        pp.query("collide_mode", collide_mode);
        if (collide_mode < 0 || collide_mode > 1) {
            amrex::Abort("lbm.collide_mode must be 0 or 1");
        }
        pp.query("stream_mode", stream_mode);
        if (stream_mode < 0 || stream_mode > 1) {
            amrex::Abort("lbm.stream_mode must be 0 (A-B) or 1 (stage-2 OSI)");
        }
        pp.query("osi_sync_batch_components", osi_sync_batch_components);
        if (osi_sync_batch_components < 1 || osi_sync_batch_components > Q) {
            amrex::Abort("lbm.osi_sync_batch_components must be in [1,Q]");
        }
        int n = pp.countval("err");
        if (n > 0) {
            pp.getarr("err", err, 0, n);
        }
    }

    {
        ParmParse pp_verify("verification");
        pp_verify.query("osi_seed_pattern", osi_verification_pattern);
        pp_verify.query("osi_ab_check", osi_ab_check);
        pp_verify.query("particle_checksum", particle_checksum);
        pp_verify.query("convergence_enabled", convergence_.enabled);
        pp_verify.query("convergence_check_int", convergence_.check_int);
        pp_verify.query("convergence_trend_int", convergence_.trend_int);
        pp_verify.query("convergence_required", convergence_.required);
        pp_verify.query("convergence_tolerance", convergence_.tolerance);
        if (convergence_.enabled) {
            AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
                convergence_.check_int > 0,
                "verification.convergence_check_int must be positive");
            AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
                convergence_.trend_int > 0,
                "verification.convergence_trend_int must be positive");
            AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
                convergence_.required > 0,
                "verification.convergence_required must be positive");
            AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
                convergence_.tolerance > 0.0,
                "verification.convergence_tolerance must be positive");
        }
    }

    {
        ParmParse pp_ckpt("checkpoint");
        pp_ckpt.query("chk_int", params_.chk_int);
        pp_ckpt.query("begin_step", params_.begin_step);
        pp_ckpt.query("chk_prefix", params_.chk_prefix);
        pp_ckpt.query("keep_latest_only", params_.keep_latest_only);
        pp_ckpt.query("write_particles", params_.write_particles);

        if (amrex::ParallelDescriptor::IOProcessor()) {
            amrex::Print() << "[Params] checkpoint:\n"
                           << "  begin_step       = " << params_.begin_step << "\n"
                           << "  chk_int          = " << params_.chk_int << "\n"
                           << "  chk_prefix       = '" << params_.chk_prefix << "'\n"
                           << "  keep_latest_only = " << (params_.keep_latest_only ? "true" : "false") << "\n"
                           << "  write_particles  = " << (params_.write_particles ? "true" : "false") << "\n";
            amrex::Print() << "[Params] amr:\n"
                           << "  plot_int         = " << params_.plot_int << "\n"
                           << "  begin_plot       = " << params_.begin_plot << "\n"
                           << "  regrid_int       = " << params_.regrid_int << "\n";
        }
    }
}

void AmrCoreLBM::WriteVelocityFile(const int step, const amrex::Real time) {
    const std::string& plotfilename = amrex::Concatenate(plot_file, step, 6);

    amrex::Vector<const amrex::MultiFab*> mf;

    for (int i = 0; i <= finest_level; ++i) {
        mf.push_back(&velocity[i]);
    }

    amrex::Vector<std::string> varnames = {"ux", "uy", "uz"};

    amrex::WriteMultiLevelPlotfile(plotfilename, finest_level + 1, mf, varnames,
                                   Geom(), time, Vector<int>(finest_level + 1, step), refRatio());
}

void AmrCoreLBM::WriteDensityFile(const int step, const amrex::Real time) {
    std::string plot_file_density{plot_file + "density_"};
    const std::string& plotfilename = amrex::Concatenate(plot_file_density, step, 6);

    amrex::Vector<const amrex::MultiFab*> mf;

    for (int i = 0; i <= finest_level; ++i) {
        mf.push_back(&density[i]);
    }

    amrex::Vector<std::string> varnames = {"rho"};

    amrex::WriteMultiLevelPlotfile(plotfilename, finest_level + 1, mf, varnames,
                                   Geom(), time, Vector<int>(finest_level + 1, step), refRatio());
}

void AmrCoreLBM::WriteVelocityFile(const int step, const amrex::Real time, const int lev) {
    const std::string& plotfilename = amrex::Concatenate(plot_file, step, 6);

    amrex::Vector<const amrex::MultiFab*> mf;

    mf.push_back(&velocity[lev]);

    amrex::Vector<std::string> varnames = {"ux", "uy", "uz"};

    amrex::WriteMultiLevelPlotfile(plotfilename, 1, mf, varnames,
                                   Geom(), time, Vector<int>(1, step), refRatio());
}

void AmrCoreLBM::WriteVorticityFile(const int step, const amrex::Real time) {
    std::string plot_file_vort{plot_file + "vort_"};
    const std::string& plotfilename = amrex::Concatenate(plot_file_vort, step, 6);

    amrex::Vector<const amrex::MultiFab*> mf;

    for (int i = 0; i <= finest_level; ++i) {
        mf.push_back(&vorticity[i]);
    }

    amrex::Vector<std::string> varnames = {"Q", "Vorticity"};

    amrex::WriteMultiLevelPlotfile(plotfilename, finest_level + 1, mf, varnames,
                                   Geom(), time, Vector<int>(finest_level + 1, step), refRatio());
}

void AmrCoreLBM::WriteParticleFile(const int step, const amrex::Real time) {
    for (int i = 0; i < particle_num; i++) {
        particles[i]->WriteParticle(step);
    }
}

void AmrCoreLBM::WriteMultiParticleFile(const int step, const amrex::Real time) {
    if (ParallelDescriptor::MyProc() == ParallelDescriptor::IOProcessorNumber()) {
        std::string filename = amrex::Concatenate("particle_data", step, 6) + ".dat";

        // 打开文件并检查是否成功
        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Cannot open the file: " << filename << std::endl;
            return;
        }

        // 写入文件内容
        file << "TITLE=particle_data\n";
        file << "VARIABLES=x,y,z,id\n";
        file << "Zone I= " << particle_num << ", J= " << AMREX_SPACEDIM + 1 << ", F= POINT\n";

        file << std::fixed << std::setprecision(6);

        // 写入粒子的坐标数据
        for (int j = 0; j < particle_num; ++j) {
            file << points[j][0] << "\t" << points[j][1] << "\t" << points[j][2] << "\t" << j << "\n";
        }

        file.close();
    }
}

//********************************************************************//
//                           mesh function                            //
//********************************************************************//
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

    for (int fine_index = 0; fine_index < fine_ba.size(); ++fine_index) {
        const Box target =
            amrex::grow(fine_ba[fine_index], fill_ng) & fine_domain;
        const BoxList leftover = fine_ba_simplified.complementIn(target); // 计算 target 中没有被 fine_ba_simplified 覆盖的区域, 并以多个互不重叠的 Box 返回
        for (const Box& work_box : leftover) {
            const Box coarse_box = coarsener.doit(work_box);
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
    // ParallelCopy 只会从 coarse valid 读取与 coarse_stage 相交的区域。
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
                     source_ba.intersections(source_query)) {
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

void AmrCoreLBM::FillCoarseInterpolationStagePhysicalBoundary(int lev) {
    auto& coarse_stage = interp_direct_coarse_stage.at(lev);
    const Box coarse_domain = Geom(lev - 1).Domain();
    const auto coarse_lo = amrex::lbound(coarse_domain);
    const auto coarse_hi = amrex::ubound(coarse_domain);
    const auto coarse_periodic = Geom(lev - 1).isPeriodicArray();

    // coarse_stage 的域外 stencil 沿最近的非周期域内单元延拓；周期方向
    // 已由调用者的 ParallelCopy(periodicity) 完成映射。
    for (MFIter mfi(coarse_stage, false); mfi.isValid(); ++mfi) {
        const int stage_index = mfi.index();
        if (!interp_direct_needs_physical_fill.at(lev).at(stage_index)) {
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

void AmrCoreLBM::FillDdfGhostFromCoarse(int lev, amrex::Real time) {
    (void)time;
    AMREX_ALWAYS_ASSERT(lev > 0 && lev <= finest_level);
    AMREX_ALWAYS_ASSERT(refRatio(lev - 1) == IntVect(2));

    auto& fine_state = f_old.at(lev);
    const auto& coarse_state = f_old.at(lev - 1);
    auto& coarse_stage = interp_direct_coarse_stage.at(lev);
    const auto& fine_work_boxes = interp_direct_fine_boxes.at(lev);
    const auto& fine_indices = interp_direct_fine_index.at(lev);
    const Real scale = tau.at(lev) / tau.at(lev - 1) / Real(2.0);

    if (fine_indices.empty()) {
        return;
    }

    ScopedPerfTimer timer(perf_stats.interp_fillpatch);
    const int interp_mode_local = interp_mode;

    // 阶段 1：把 canonical coarse valid/周期像装入稀疏 stencil。
    coarse_stage.ParallelCopy(
        coarse_state, 0, 0, Q, IntVect(0), IntVect(0),
        Geom(lev - 1).periodicity());
    // 阶段 2：两条插值路径共享相同的物理边界规则。
    FillCoarseInterpolationStagePhysicalBoundary(lev);

    // 阶段 3/4：缩放当前 stencil，随后写入 canonical fine 布局。
    for (MFIter mfi(coarse_stage, false); mfi.isValid(); ++mfi) {
        const int stage_index = mfi.index();
        const Box stage_box = mfi.validbox();
        const auto coarse = coarse_stage.array(mfi);
        amrex::ParallelFor(
            stage_box, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                average_scale(i, j, k, coarse, scale);
            });

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
    BuildRestrictionCache();
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
        const Box covered_neighbor_domain =
            Geom(lev).growPeriodicDomain(1);
        const auto neighbor_lo = amrex::lbound(covered_neighbor_domain);
        const auto neighbor_hi = amrex::ubound(covered_neighbor_domain);

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
                            const bool in_covered_neighbor_domain =
                                (ni >= neighbor_lo.x &&
                                 ni <= neighbor_hi.x) &&
                                (nj >= neighbor_lo.y &&
                                 nj <= neighbor_hi.y) &&
                                (nk >= neighbor_lo.z &&
                                 nk <= neighbor_hi.z);
                            if (in_covered_neighbor_domain ||
                                covered(ni, nj, nk) == 0) {
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

void AmrCoreLBM::BuildRestrictionCache() {
    for (int lev = 0; lev < finest_level; ++lev) {
        const MultiFab& fine_layout =
            stream_mode == 1 ? osi_state.at(lev + 1) : f_old.at(lev + 1);
        BoxArray coarse_from_fine =
            amrex::coarsen(fine_layout.boxArray(), refRatio(lev));

        BoxList interface_boxes;
        Vector<int> interface_owners;
        Vector<int> fine_box_indices;
        const Box domain = Geom(lev).Domain();
        const auto& periodicity = Geom(lev).periodicity();
        const auto& fine_dm = fine_layout.DistributionMap();

        for (int ibox = 0; ibox < coarse_from_fine.size(); ++ibox) {
            const Box& covered_box = coarse_from_fine[ibox];
            const Box search_box = amrex::grow(covered_box, 1) & domain;
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

//********************************************************************//
//                           lbm  function                            //
//********************************************************************//

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
            const box3d_osi::FabGeometry fab{
                {fab_lo[0], fab_lo[1], fab_lo[2]},
                {ring_box.length(0), ring_box.length(1), ring_box.length(2)}};
            const Array4<const Real>& ddf = state.const_array(mfi);
            const Array4<Real>& rho = rho_lev.array(mfi);
            const Array4<Real>& u = u_lev.array(mfi);

            amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                compute_macro_osi(i, j, k, ddf, rho, u, phase, fab);
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
    // Synchronize covered coarse cells once, in finest-to-coarse order,
    // before computing the macro variables on every level.
    AverageDownValid();
    for (int lev = 0; lev <= finest_level; lev++) {
        ComputeMacroLevel(lev);
    }
}

std::array<Real, 3> AmrCoreLBM::MeasureLevel0VelocityChange(
    const MultiFab& previous) const {
    const MultiFab& velocity_0 = velocity.at(0);
    MultiFab velocity_delta(
        velocity_0.boxArray(), velocity_0.DistributionMap(),
        AMREX_SPACEDIM, 0);
    MultiFab::Copy(
        velocity_delta, velocity_0, 0, 0, AMREX_SPACEDIM, 0);
    MultiFab::Subtract(
        velocity_delta, previous, 0, 0, AMREX_SPACEDIM, 0);
    Gpu::synchronize();

    Real numerator_sq = 0.0;
    Real denominator_sq = 0.0;
    for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
        const Real diff_l2 = velocity_delta.norm2(dir, 1);
        const Real velocity_l2 = velocity_0.norm2(dir, 1);
        numerator_sq += diff_l2 * diff_l2;
        denominator_sq += velocity_l2 * velocity_l2;
    }

    const Real delta_velocity_l2 = std::sqrt(numerator_sq);
    const Real velocity_l2 = std::sqrt(denominator_sq);
    return {delta_velocity_l2 /
                amrex::max(velocity_l2, std::numeric_limits<Real>::min()),
            velocity_l2, delta_velocity_l2};
}

void AmrCoreLBM::InitializeConvergence() {
    if (!convergence_.enabled) {
        return;
    }

    ComputeMacro();
    Gpu::synchronize();

    const MultiFab& velocity_0 = velocity.at(0);
    convergence_.previous_check_velocity = std::make_unique<MultiFab>(
        velocity_0.boxArray(), velocity_0.DistributionMap(),
        AMREX_SPACEDIM, 0);
    convergence_.previous_trend_velocity = std::make_unique<MultiFab>(
        velocity_0.boxArray(), velocity_0.DistributionMap(),
        AMREX_SPACEDIM, 0);
    MultiFab::Copy(*convergence_.previous_check_velocity, velocity_0, 0, 0,
                   AMREX_SPACEDIM, 0);
    MultiFab::Copy(*convergence_.previous_trend_velocity, velocity_0, 0, 0,
                   AMREX_SPACEDIM, 0);
    convergence_.streak = 0;
}

bool AmrCoreLBM::CheckConvergence(int step) {
    if (!convergence_.enabled) {
        return false;
    }

    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        convergence_.previous_check_velocity &&
            convergence_.previous_trend_velocity,
        "InitializeConvergence must be called before CheckConvergence");
    const int elapsed_steps = step - params_.begin_step;
    const bool convergence_sample = elapsed_steps % convergence_.check_int == 0;
    const bool trend_sample = elapsed_steps % convergence_.trend_int == 0;
    if (!convergence_sample && !trend_sample) {
        return false;
    }

    ComputeMacro();
    Gpu::synchronize();
    const MultiFab& velocity_0 = velocity.at(0);
    if (trend_sample) {
        const auto values =
            MeasureLevel0VelocityChange(*convergence_.previous_trend_velocity);
        amrex::Print() << "CONVERGENCE_TREND step=" << step
                       << " interval=" << convergence_.trend_int
                       << " velocity_l2_relative=" << values[0]
                       << " velocity_l2=" << values[1]
                       << " delta_velocity_l2=" << values[2] << '\n';
        MultiFab::Copy(*convergence_.previous_trend_velocity, velocity_0, 0, 0,
                       AMREX_SPACEDIM, 0);
    }

    if (!convergence_sample) {
        return false;
    }

    const auto values =
        MeasureLevel0VelocityChange(*convergence_.previous_check_velocity);
    convergence_.streak = values[0] < convergence_.tolerance
                              ? convergence_.streak + 1
                              : 0;
    const bool converged = convergence_.streak >= convergence_.required;
    amrex::Print() << "CONVERGENCE step=" << step
                   << " interval=" << convergence_.check_int
                   << " velocity_l2_relative=" << values[0]
                   << " velocity_l2=" << values[1]
                   << " delta_velocity_l2=" << values[2]
                   << " tolerance=" << convergence_.tolerance
                   << " consecutive=" << convergence_.streak
                   << '/' << convergence_.required
                   << " converged=" << converged << '\n';
    MultiFab::Copy(*convergence_.previous_check_velocity, velocity_0, 0, 0,
                   AMREX_SPACEDIM, 0);
    return converged;
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

void AmrCoreLBM::AverageDownOsiValidLevel(int lev, bool is_scale) {
    AMREX_ALWAYS_ASSERT(lev >= 0 && lev < finest_level);
    auto& coarse_state = osi_state.at(lev);
    const auto& fine_state = osi_state.at(lev + 1);
    const IntVect ratio = refRatio(lev);
    const auto fine_phase = osi_phase.at(lev + 1);
    const auto coarse_phase = osi_phase.at(lev);
    const Real scale = Real(2.0) * tau.at(lev) / tau.at(lev + 1);

    // This path is used only for an intermediate AMR level.  Decode complete
    // canonical valid arrays and deliberately reuse the exact A-B scaling and
    // AMReX average_down operation.  The temporary storage is more expensive
    // than the interface-only OSI path, but avoids a different restriction
    // implementation becoming part of the four-level correctness result.
    MultiFab fine_canonical(
        fine_state.boxArray(), fine_state.DistributionMap(), Q, 0);
    MultiFab coarse_canonical(
        coarse_state.boxArray(), coarse_state.DistributionMap(), Q, 0);

    for (int q0 = 0; q0 < Q; q0 += osi_sync_batch_components) {
        const int ncomp = amrex::min(osi_sync_batch_components, Q - q0);
        MultiFab& fine_batch = osi_sync_buffer.at(lev + 1);
        MultiFab& coarse_batch = osi_sync_buffer.at(lev);
        DecodeOsiValidBatch(
            fine_state, fine_phase, fine_batch, q0, ncomp);
        DecodeOsiValidBatch(
            coarse_state, coarse_phase, coarse_batch, q0, ncomp);
        MultiFab::Copy(fine_canonical, fine_batch, 0, q0, ncomp, 0);
        MultiFab::Copy(coarse_canonical, coarse_batch, 0, q0, ncomp, 0);
    }

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
        const auto lo = ring.smallEnd();
        const box3d_osi::FabGeometry coarse_fab{
            {lo[0], lo[1], lo[2]},
            {ring.length(0), ring.length(1), ring.length(2)}};
        const auto src = coarse_canonical.const_array(mfi);
        const auto dst = coarse_state.array(mfi);
        amrex::ParallelFor(
            bx, Q,
            [=] AMREX_GPU_DEVICE(int i, int j, int k, int q) {
                const auto raw = box3d_osi::osi_address(
                    {i, j, k}, {e[q][0], e[q][1], e[q][2]},
                    coarse_phase, coarse_fab);
                dst(raw.x, raw.y, raw.z, q) = src(i, j, k, q);
            });
    }
    amrex::Gpu::streamSynchronize();
}

void AmrCoreLBM::AverageDownValid() {
    // interface-only 时间推进不会更新深层 covered 粗单元；regrid 可能重新暴露这些单元，
    // 因此重网格前必须先将全部细层 valid 数据完整同步到粗层父单元。
    for (int lev = finest_level - 1; lev >= 0; --lev) {
        if (stream_mode == 1) {
            AverageDownOsiValidLevel(lev, true);
        } else {
            AverageDownValidLevel(lev, true);
        }
    }
}

// void AmrCoreLBM::AverageDownGhostLevel(int lev)
// {
//     amrex::MultiFab& fine_mf = f_old[lev+1];
//     amrex::MultiFab& crse_mf = f_old[lev];
//     BoxArray fine_boundary_grids;

//     BoxArray const& fbas = fine_mf.boxArray();
//     BoxList bl;

//     for(int i = 0; i < fbas.size(); ++i)
//     {
//         Box const& b = amrex::grow(fbas[i], 2);
//         auto const& bltmp = fbas.complementIn(b);
//         bl.join(bltmp);
//     }

//     fine_boundary_grids.define(std::move(bl));

//     DistributionMapping fine_boundary_dmap(fine_boundary_grids);

//     MultiFab fine_boundary_data(fine_boundary_grids, fine_boundary_dmap, Q, 0);

//     fine_boundary_data.ParallelCopy(fine_mf, 0, 0, Q, 2, 0);

//     amrex::average_down(fine_boundary_data, crse_mf, 0, Q, refRatio(lev));
// }

void AmrCoreLBM::AverageDownGhostLevel(int lev, bool is_scale) {
    ScopedPerfTimer timer(perf_stats.average);
    ++perf_stats.avgdown_calls;

    if (lev >= finest_level) {
        return;
    }

    if (stream_mode == 1) {
        AverageDownOsiLevel(lev, is_scale);
        return;
    }

    amrex::MultiFab& fine_mf = f_old[lev + 1];
    amrex::MultiFab& crse_mf = f_old[lev];

    const IntVect ratio = refRatio(lev);
    // 平均前先将细层非平衡分布函数转换到粗层松弛时间对应的尺度。
    const Real scale = 2.0 * tau[lev] / tau[lev + 1];
    ScopedPerfTimer avgdown_timer(perf_stats.average_down);

    {
        const Long children_per_parent = ratio[0] * ratio[1] * ratio[2];
        // 交界区域融合路径只处理缓存的 coarse-interface 父单元。
        MultiFab& interface_result = average_interface_buffer[lev];
        const Vector<int>& fine_box_indices = average_interface_fine_box[lev];
        if (fine_box_indices.empty()) {
            return;
        }

        AMREX_ALWAYS_ASSERT(interface_result.size() == fine_box_indices.size());

        {
            // 缩放与限制融合，不写回完整的缩放后 DDF 中间数据。
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
}

void AmrCoreLBM::AverageDownGhost() {
}

void AmrCoreLBM::FillGhostLevel(int lev, amrex::Real time, bool is_scale) {
    ScopedPerfTimer timer(perf_stats.interp);
    ++perf_stats.fillghost_calls;

    if (stream_mode == 1) {
        if (is_scale) {
            FillOsiGhostFromCoarse(lev, time);
        }
    } else if (is_scale) {
        FillDdfGhostFromCoarse(lev, time);
    } else {
        FillPatch(lev, time, f_old.at(lev));
    }
}

void AmrCoreLBM::FillOsiGhostFromCoarse(int lev, amrex::Real time) {
    (void)time;
    AMREX_ALWAYS_ASSERT(lev > 0 && lev <= finest_level);
    AMREX_ALWAYS_ASSERT(refRatio(lev - 1) == IntVect(2));
    AMREX_ALWAYS_ASSERT(interp_direct_cache_ready.at(lev));

    auto& coarse_state = osi_state.at(lev - 1);
    auto& fine_state = osi_state.at(lev);
    auto& decode_batch = osi_sync_buffer.at(lev - 1);
    auto& coarse_stage = interp_direct_coarse_stage.at(lev);
    const auto& fine_work_boxes = interp_direct_fine_boxes.at(lev);
    const auto& fine_indices = interp_direct_fine_index.at(lev);
    const auto coarse_phase = osi_phase.at(lev - 1);
    const auto fine_phase = osi_phase.at(lev);
    const Real scale = tau.at(lev) / tau.at(lev - 1) / Real(2.0);

    if (fine_indices.empty()) {
        return;
    }

    // 分批把粗层 valid 区从 OSI raw 地址解码到现有通信缓冲区，再只复制
    // 实际插值 stencil 到 sparse coarse_stage。这里不再构造整层 Q 分量
    // 后续需要优化,实现OSI适配版的ParallelCopy函数
    for (int q0 = 0; q0 < Q; q0 += osi_sync_batch_components) {
        const int ncomp =
            amrex::min(osi_sync_batch_components, Q - q0);

        for (MFIter mfi(coarse_state, false); mfi.isValid(); ++mfi) {
            const auto src = coarse_state.const_array(mfi);
            const auto dst = decode_batch.array(mfi);

            const Box ring =
                amrex::grow(mfi.validbox(), coarse_state.nGrowVect());
            const auto lo = ring.smallEnd();

            const box3d_osi::FabGeometry fab{
                {lo[0], lo[1], lo[2]},
                {ring.length(0), ring.length(1), ring.length(2)}};

            for (const Box& bx :
                 osi_interp_decode_boxes.at(lev).at(mfi.index())) {
                amrex::ParallelFor(
                    bx, ncomp,
                    [=] AMREX_GPU_DEVICE(int i, int j, int k, int n) {
                        const int q = q0 + n;
                        const auto raw = box3d_osi::osi_address(
                            {i, j, k},
                            {e[q][0], e[q][1], e[q][2]},
                            coarse_phase, fab);

                        dst(i, j, k, n) = src(raw.x, raw.y, raw.z, q);
                    });
            }
        }
        coarse_stage.ParallelCopy(
            decode_batch, 0, q0, ncomp, IntVect(0), IntVect(0),
            Geom(lev - 1).periodicity());
    }

    FillCoarseInterpolationStagePhysicalBoundary(lev);

    for (MFIter mfi(coarse_stage, false); mfi.isValid(); ++mfi) {
        const int stage_index = mfi.index();
        const Box stage_box = mfi.validbox();
        const auto coarse = coarse_stage.array(mfi);
        amrex::ParallelFor(
            stage_box, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                average_scale(i, j, k, coarse, scale);
            });

        const int fine_index = fine_indices.at(stage_index);
        const Box fine_ring = amrex::grow(
            fine_state.boxArray()[fine_index], fine_state.nGrowVect());
        const auto fine_lo = fine_ring.smallEnd();
        const box3d_osi::FabGeometry fine_fab{
            {fine_lo[0], fine_lo[1], fine_lo[2]},
            {fine_ring.length(0), fine_ring.length(1), fine_ring.length(2)}};

        const auto fine = fine_state.array(fine_index);
        const auto coarse_const = coarse_stage.const_array(mfi);
        const Box fine_box = fine_work_boxes.at(stage_index);
        if (interp_mode == 0) {
            amrex::ParallelFor(
                fine_box, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                    interp_bilinear_d3q(
                        i, j, k, fine, coarse_const, fine_fab, fine_phase);
                });
        } else {
            const Box coarse_parent_box = amrex::coarsen(fine_box, 2);
            if (interp_mode == 1) {
                amrex::ParallelFor(
                    coarse_parent_box,
                    [=] AMREX_GPU_DEVICE(int ic, int jc, int kc) {
                        interp_cell_cons_linear_children_d3q(
                            ic, jc, kc, fine, coarse_const, fine_box,
                            fine_fab, fine_phase);
                    });
            } else {
                amrex::ParallelFor(
                    coarse_parent_box,
                    [=] AMREX_GPU_DEVICE(int ic, int jc, int kc) {
                        interp_cell_quadratic_children_d3q(
                            ic, jc, kc, fine, coarse_const, fine_box,
                            fine_fab, fine_phase);
                    });
            }
        }
    }
}

void AmrCoreLBM::AverageDownOsiLevel(int lev, bool is_scale) {
    auto& coarse_state = osi_state.at(lev);
    const auto& fine_state = osi_state.at(lev + 1);
    auto& interface_result = average_interface_buffer.at(lev);
    auto& transfer_batch = osi_sync_buffer.at(lev);
    const auto& fine_indices = average_interface_fine_box.at(lev);
    const auto ratio = refRatio(lev);
    const auto coarse_phase = osi_phase.at(lev);
    const auto fine_phase = osi_phase.at(lev + 1);
    const Real scale = Real(2.0) * tau.at(lev) / tau.at(lev + 1);

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
        const auto fine_lo = fine_ring.smallEnd();
        const box3d_osi::FabGeometry fine_fab{
            {fine_lo[0], fine_lo[1], fine_lo[2]},
            {fine_ring.length(0), fine_ring.length(1), fine_ring.length(2)}};
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
                        fine_phase, scale, is_scale);
                }
            });
#else
        amrex::ParallelFor(
            bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                average_down_osi_to_canonical(
                    i, j, k, coarse, fine, ratio, fine_fab, fine_phase,
                    scale, is_scale);
            });
#endif
    }

    // interface_result 沿用 fine owner。分批 ParallelCopy 把结果送到真实
    // coarse owner，再只编码 interface mask，避免陈旧缓冲覆盖其他粗单元。
    for (int q0 = 0; q0 < Q; q0 += osi_sync_batch_components) {
        const int ncomp = amrex::min(osi_sync_batch_components, Q - q0);

        transfer_batch.ParallelCopy(
            interface_result, q0, 0, ncomp, IntVect(0), IntVect(0));

        for (MFIter mfi(coarse_state, false); mfi.isValid(); ++mfi) {
            const Box ring =
                amrex::grow(mfi.validbox(), coarse_state.nGrowVect());
            const auto lo = ring.smallEnd();
            const box3d_osi::FabGeometry coarse_fab{
                {lo[0], lo[1], lo[2]},
                {ring.length(0), ring.length(1), ring.length(2)}};
            const Box bx = mfi.validbox();
            const auto mask = interface_mask.at(lev).const_array(mfi);
            const auto src = transfer_batch.const_array(mfi);
            const auto dst = coarse_state.array(mfi);
            amrex::ParallelFor(
                bx, ncomp,
                [=] AMREX_GPU_DEVICE(int i, int j, int k, int n) {
                    if (mask(i, j, k) == 0) {
                        return;
                    }
                    const int q = q0 + n;
                    const auto raw = box3d_osi::osi_address(
                        {i, j, k}, {e[q][0], e[q][1], e[q][2]},
                        coarse_phase, coarse_fab);
                    dst(raw.x, raw.y, raw.z, q) = src(i, j, k, n);
                });
        }
        amrex::Gpu::streamSynchronize();
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
        CommunicateOsiLevel(lev);
        return;
    }

    amrex::MultiFab& f_old_lev = f_old[lev];
    f_old_lev.FillBoundary(geom[lev].periodicity());
}

void AmrCoreLBM::ValidateConfiguration() const {
    if (stream_mode != 1) {
        return;
    }

    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        max_level <= max_ref_level && finest_level <= max_ref_level,
        "OSI AMR level exceeds the number of levels compiled into the LBM model");
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        collide_mode == 1,
        "OSI currently supports only lbm.collide_mode=1");
    if (max_level > 1) {
        amrex::Print()
            << "[OSI validation warning] More than two AMR levels are enabled. "
               "Finite-state/output checks are active, but four-level OSI/A-B "
               "cellwise equivalence has not yet met the 1e-12 criterion.\n";
    }
    for (int lev = 0; lev <= finest_level; ++lev) {
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            osi_state.at(lev).isDefined() &&
                osi_sync_buffer.at(lev).isDefined(),
            "OSI state or synchronization buffer was not initialized for an active AMR level");
    }

    bool all_periodic = true;
    for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
        all_periodic = all_periodic && Geom(0).isPeriodic(dir);
    }

    amrex::Print() << (all_periodic ? "[OSI periodic] ranks=" : "[OSI boundary] ranks=")
                   << amrex::ParallelDescriptor::NProcs()
                   << " active_levels=" << finest_level + 1
                   << " seed_pattern=" << (osi_verification_pattern ? 1 : 0)
                   << " ab_check=" << (osi_ab_check ? 1 : 0)
                   << " full_ddf_arrays=" << (osi_ab_check ? 3 : 1)
                   << " sync_batch_components=" << osi_sync_batch_components
                   << " sync_batches="
                   << ((Q + osi_sync_batch_components - 1) /
                       osi_sync_batch_components)
                   << " grown-fab overlap synchronization enabled\n";

    for (int lev = 0; lev <= finest_level; ++lev) {
        amrex::Long state_values = 0;
        const BoxArray& state_ba = osi_state[lev].boxArray();
        const IntVect state_ng = osi_state[lev].nGrowVect();
        for (int ibox = 0; ibox < state_ba.size(); ++ibox) {
            state_values +=
                amrex::grow(state_ba[ibox], state_ng).numPts() * Q;
        }
        amrex::Long sync_values = 0;
        const BoxArray& sync_ba = osi_sync_buffer[lev].boxArray();
        const IntVect sync_ng = osi_sync_buffer[lev].nGrowVect();
        for (int ibox = 0; ibox < sync_ba.size(); ++ibox) {
            sync_values += amrex::grow(sync_ba[ibox], sync_ng).numPts() *
                           osi_sync_buffer[lev].nComp();
        }
        amrex::Print() << "[OSI level] level=" << lev
                       << " boxes=" << state_ba.size()
                       << " state_values=" << state_values
                       << " sync_values=" << sync_values
                       << " ring_ngrow=" << state_ng[0]
                       << " phase=" << osi_phase[lev] << '\n';
    }
}

void AmrCoreLBM::ValidateInitializedState(const char* context) {
    for (int lev = 0; lev <= finest_level; ++lev) {
        const MultiFab& state =
            stream_mode == 1 ? osi_state.at(lev) : f_old.at(lev);
        MultiFab* decoded =
            stream_mode == 1 ? &osi_sync_buffer.at(lev) : nullptr;
        const bool has_fine = lev < finest_level && cf_mask_mode == 1;
        MultiFab active_scalar(
            state.boxArray(), state.DistributionMap(), 1, 0);

        // Deep-covered coarse cells are deliberately skipped by Collide/Stream
        // and may contain stale or undefined values after a layout change.
        // Validation must follow the same ownership rule as the solver instead
        // of treating those inactive cells as part of the numerical solution.
        // Physical validity is defined on uncovered cells only.  Interface
        // covered cells are an AMR coupling diagnostic, not solver-owned
        // physical cells and may legitimately be stale after a regrid.
        const auto active_stats =
            [&](const MultiFab& source, int comp, Real inactive_value) {
                if (!has_fine) {
                    return GpuArray<Real, 3>{source.min(comp, 0),
                                             source.max(comp, 0),
                                             source.sum(comp, 0)};
                }
                for (MFIter mfi(source, false); mfi.isValid(); ++mfi) {
                    const Box bx = mfi.validbox();
                    const auto src = source.const_array(mfi);
                    const auto dst = active_scalar.array(mfi);
                    const auto covered =
                        covered_mask.at(lev).const_array(mfi);
                    amrex::ParallelFor(
                        bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                            dst(i, j, k) = (covered(i, j, k) == 0)
                                               ? src(i, j, k, comp)
                                               : inactive_value;
                        });
                }
                return GpuArray<Real, 3>{active_scalar.min(0, 0),
                                         active_scalar.max(0, 0),
                                         active_scalar.sum(0, 0)};
            };
        Real ddf_min = std::numeric_limits<Real>::max();
        Real ddf_max = std::numeric_limits<Real>::lowest();

        for (int q0 = 0; q0 < Q; q0 += osi_sync_batch_components) {
            const int ncomp =
                amrex::min(osi_sync_batch_components, Q - q0);
            const MultiFab* canonical = &state;
            if (stream_mode == 1) {
                DecodeOsiValidBatch(state, osi_phase.at(lev), *decoded,
                                    q0, ncomp);
                canonical = decoded;
            }
            for (int n = 0; n < ncomp; ++n) {
                const int comp = stream_mode == 1 ? n : q0 + n;
                const auto stats = active_stats(*canonical, comp, Real(0.0));
                const Real q_min = stats[0];
                const Real q_max = stats[1];
                const Real q_sum = stats[2];
                AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
                    std::isfinite(q_min) && std::isfinite(q_max) &&
                        std::isfinite(q_sum) && q_min <= q_max,
                    "Initialized valid DDF contains a NaN or infinity");
                ddf_min = amrex::min(ddf_min, q_min);
                ddf_max = amrex::max(ddf_max, q_max);
            }
        }

        ComputeMacroLevel(lev);
        const auto rho_stats =
            active_stats(density.at(lev), 0, Real(1.0));
        const Real rho_min = rho_stats[0];
        const Real rho_max = rho_stats[1];
        const Real rho_sum = rho_stats[2];
        Real velocity_linf = 0.0;
        for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
            const auto velocity_stats =
                active_stats(velocity.at(lev), dir, Real(0.0));
            velocity_linf = amrex::max(
                velocity_linf,
                amrex::max(std::abs(velocity_stats[0]),
                           std::abs(velocity_stats[1])));
        }
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            std::isfinite(rho_min) && std::isfinite(rho_max) &&
                std::isfinite(rho_sum) && rho_min <= rho_max &&
                std::isfinite(velocity_linf) && rho_min > 0.0,
            "Initialized macroscopic state is invalid");
        amrex::Print() << '[' << context << "] level=" << lev
                       << " phase="
                       << (stream_mode == 1 ? osi_phase.at(lev) : 0)
                       << " ddf_min=" << ddf_min
                       << " ddf_max=" << ddf_max
                       << " rho_min=" << rho_min
                       << " rho_max=" << rho_max
                       << " velocity_linf=" << velocity_linf << '\n';
    }
}

void AmrCoreLBM::CommunicateOsiLevel(int lev) {
    amrex::MultiFab& state = osi_state.at(lev);
    amrex::MultiFab& canonical = osi_sync_buffer.at(lev);
    const std::uint64_t phase = osi_phase.at(lev);
    const Periodicity& periodicity = geom[lev].periodicity();
    const IntVect ng = state.nGrowVect();

    for (int q0 = 0; q0 < Q; q0 += osi_sync_batch_components) {
        const int batch_size = std::min(osi_sync_batch_components, Q - q0);

        {
            ScopedPerfTimer timer(perf_stats.osi_decode);
            // Decode：只解码 FillBoundary 会读取的 owner-valid 源区。缓存 Box
            // 已去重，并通过 TagVector 合并为本 rank 每批一次 GPU launch。
            amrex::ParallelFor(
                osi_decode_tags.at(lev), batch_size,
                [=] AMREX_GPU_DEVICE(
                    int i, int j, int k, int n,
                    const box3d_osi::CommunicationTag& tag) noexcept {
                    const int q = q0 + n;
                    const auto raw = box3d_osi::osi_address(
                        {i, j, k}, {e[q][0], e[q][1], e[q][2]}, phase,
                        tag.fab);
                    tag.dst(i, j, k, n) = tag.src(raw.x, raw.y, raw.z, q);
                });
        }

        // 同 rank、跨 rank 和周期像均按“同一逻辑坐标的副本”同步。
        {
            ScopedPerfTimer timer(perf_stats.osi_fillboundary);
            canonical.FillBoundary(0, batch_size, ng, periodicity);
        }

        {
            ScopedPerfTimer timer(perf_stats.osi_encode);
            // Encode：只回写 FillBoundary 实际更新的目标 ghost。valid 已在碰撞
            // 后保存在 state 中，既不需要也不应从 canonical 重复写回。
            amrex::ParallelFor(
                osi_encode_tags.at(lev), batch_size,
                [=] AMREX_GPU_DEVICE(
                    int i, int j, int k, int n,
                    const box3d_osi::CommunicationTag& tag) noexcept {
                    const int q = q0 + n;
                    const auto raw = box3d_osi::osi_address(
                        {i, j, k}, {e[q][0], e[q][1], e[q][2]}, phase,
                        tag.fab);
                    tag.dst(raw.x, raw.y, raw.z, q) = tag.src(i, j, k, n);
                });
        }
    }
}

void AmrCoreLBM::BuildOsiCommunicationRegionCache(int lev) {
    const BoxArray& ba = osi_state.at(lev).boxArray();
    const IntVect ng = osi_state.at(lev).nGrowVect();
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
                    const Box destination_box = exact_source + shift;

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

    Vector<box3d_osi::CommunicationTag> decode_tags;
    Vector<box3d_osi::CommunicationTag> encode_tags;
    decode_tags.reserve(static_cast<std::size_t>(decode_box_count)); // 预留空间
    encode_tags.reserve(static_cast<std::size_t>(encode_box_count));
    MultiFab& state = osi_state.at(lev);
    MultiFab& canonical = osi_sync_buffer.at(lev);
    for (MFIter mfi(canonical, false); mfi.isValid(); ++mfi) {
        const int ibox = mfi.index();
        const Box ring_box = amrex::grow(ba[ibox], ng);
        const auto fab_lo = ring_box.smallEnd();
        const box3d_osi::FabGeometry fab{
            {fab_lo[0], fab_lo[1], fab_lo[2]},
            {ring_box.length(0), ring_box.length(1), ring_box.length(2)}};
        const Array4<const Real> state_src = state.const_array(ibox);
        const Array4<Real> state_dst = state.array(ibox);
        const Array4<const Real> canonical_src = canonical.const_array(mfi);
        const Array4<Real> canonical_dst = canonical.array(mfi);
        for (const Box& bx : decode[ibox]) {
            decode_tags.push_back({state_src, canonical_dst, bx, fab});
        }
        for (const Box& bx : encode[ibox]) {
            encode_tags.push_back({canonical_src, state_dst, bx, fab});
        }
    }
    osi_decode_tags.at(lev).define(decode_tags);
    osi_encode_tags.at(lev).define(encode_tags);

    amrex::Print() << "[OSI comm cache] level=" << lev
                   << " decode_boxes=" << decode_box_count
                   << " decode_cells=" << decode_cells
                   << " full_decode_cells=" << full_decode_cells
                   << " encode_boxes=" << encode_box_count
                   << " encode_cells=" << encode_cells
                   << " full_encode_cells=" << full_encode_cells << '\n';
}

void AmrCoreLBM::AdvanceLevel(int lev) {
    const DdfLayout layout =
        stream_mode == 1 ? DdfLayout::Osi : DdfLayout::Canonical;
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        layout == DdfLayout::Canonical || osi_state.at(lev).isDefined(),
        "AdvanceLevel requires an initialized OSI state");

    // 两种存储模式共享完全相同的物理阶段顺序；各阶段只在真正访问
    // DDF 或执行数据移动时选择 canonical/OSI 实现。
    Collide(lev, nghost, layout);
    CommunicateLevel(lev, layout);
    Stream(lev, nghost, layout);
    Boundary(lev, layout);
    SwapLevel(lev, nghost, layout);
}

void AmrCoreLBM::AdvanceAndCheckOsiReference(int lev, int step) {
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        stream_mode == 1,
        "AdvanceAndCheckOsiReference requires lbm.stream_mode=1");
    if (!osi_ab_check) {
        return;
    }

    // 保留的 A-B 状态作为 OSI 逐步 oracle：valid collision、ghost 同步、
    // 显式 pull、物理边界和 swap。
    constexpr DdfLayout reference_layout = DdfLayout::Canonical;
    Collide(lev, 0, reference_layout);
    CommunicateLevel(lev, reference_layout);
    Stream(lev, 1, reference_layout);
    Boundary(lev, reference_layout);
    SwapLevel(lev, 1, reference_layout);

    amrex::MultiFab& difference = f_new[lev];
    const amrex::MultiFab& reference = f_old[lev];
    const amrex::MultiFab& state = osi_state[lev];
    const std::uint64_t phase = osi_phase[lev];

    for (MFIter mfi(reference, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const Box valid_box = mfi.validbox();
        const Box ring_box = amrex::grow(valid_box, state.nGrowVect());
        const auto fab_lo = ring_box.smallEnd();
        const box3d_osi::FabGeometry fab{
            {fab_lo[0], fab_lo[1], fab_lo[2]},
            {ring_box.length(0), ring_box.length(1), ring_box.length(2)}};
        const Array4<const Real>& ab = reference.const_array(mfi);
        const Array4<const Real>& osi = state.const_array(mfi);
        const Array4<Real>& diff = difference.array(mfi);

        amrex::ParallelFor(
            valid_box, Q,
            [=] AMREX_GPU_DEVICE(int i, int j, int k, int q) {
                const auto raw = box3d_osi::osi_address(
                    {i, j, k}, {e[q][0], e[q][1], e[q][2]}, phase, fab);
                diff(i, j, k, q) =
                    ab(i, j, k, q) - osi(raw.x, raw.y, raw.z, q);
            });
    }

    const Real linf = difference.norm0(0, Q, IntVect(0));
    constexpr Real tolerance = 1.0e-12;
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        std::isfinite(linf) && linf <= tolerance,
        "OSI differs from the A-B reference (linf > 1e-12)");
    ComputeMacroLevel(lev);
    amrex::Print() << "osi_ab: step=" << step
                   << " phase=" << osi_phase[lev]
                   << " linf=" << linf << '\n';
}

void AmrCoreLBM::PrintDdfChecksums(int step) {
    for (int lev = 0; lev <= finest_level; ++lev) {
        const bool has_fine = lev < finest_level && cf_mask_mode == 1;
        Real valid_checksum = 0.0;
        Real active_checksum = 0.0;
        GpuArray<Real, Q> active_q{};

        if (stream_mode == 1) {
            const MultiFab& state = osi_state.at(lev);
            MultiFab& batch = osi_sync_buffer.at(lev);
            const auto phase = osi_phase.at(lev);

            // 诊断只复用现有分批同步缓冲，不再把整层 OSI 状态解码到 f_old/f_new。
            for (int q0 = 0; q0 < Q; q0 += osi_sync_batch_components) {
                const int ncomp =
                    amrex::min(osi_sync_batch_components, Q - q0);
                for (MFIter mfi(state, false); mfi.isValid(); ++mfi) {
                    const Box bx = mfi.validbox();
                    const Box ring =
                        amrex::grow(bx, state.nGrowVect());
                    const auto lo = ring.smallEnd();
                    const box3d_osi::FabGeometry fab{
                        {lo[0], lo[1], lo[2]},
                        {ring.length(0), ring.length(1), ring.length(2)}};
                    const auto src = state.const_array(mfi);
                    const auto dst = batch.array(mfi);
                    amrex::ParallelFor(
                        bx, ncomp,
                        [=] AMREX_GPU_DEVICE(int i, int j, int k, int n) {
                            const int q = q0 + n;
                            const auto raw = box3d_osi::osi_address(
                                {i, j, k},
                                {e[q][0], e[q][1], e[q][2]}, phase, fab);
                            dst(i, j, k, n) = src(raw.x, raw.y, raw.z, q);
                        });
                }

                for (int n = 0; n < ncomp; ++n) {
                    const int q = q0 + n;
                    const Real valid_q = batch.sum(n, 0);
                    valid_checksum += valid_q;
                    if (!has_fine) {
                        active_q[q] = valid_q;
                        active_checksum += valid_q;
                        continue;
                    }

                    for (MFIter mfi(batch, false); mfi.isValid(); ++mfi) {
                        const Box bx = mfi.validbox();
                        const auto values = batch.array(mfi);
                        const auto covered =
                            covered_mask.at(lev).const_array(mfi);
                        amrex::ParallelFor(
                            bx,
                            [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                                const bool active = covered(i, j, k) == 0;
                                if (!active) {
                                    values(i, j, k, n) = Real(0.0);
                                }
                            });
                    }
                    active_q[q] = batch.sum(n, 0);
                    active_checksum += active_q[q];
                }
            }
        } else {
            const MultiFab& state = f_old.at(lev);
            MultiFab active_component(
                state.boxArray(), state.DistributionMap(), 1, 0);
            for (int q = 0; q < Q; ++q) {
                for (MFIter mfi(state, false); mfi.isValid(); ++mfi) {
                    const Box bx = mfi.validbox();
                    const auto src = state.const_array(mfi);
                    const auto dst = active_component.array(mfi);
                    if (!has_fine) {
                        amrex::ParallelFor(
                            bx,
                            [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                                dst(i, j, k) = src(i, j, k, q);
                            });
                        continue;
                    }
                    const auto covered = covered_mask.at(lev).const_array(mfi);
                    amrex::ParallelFor(
                        bx,
                        [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                            const bool active = covered(i, j, k) == 0;
                            dst(i, j, k) =
                                active ? src(i, j, k, q) : Real(0.0);
                        });
                }
                valid_checksum += state.sum(q, 0);
                active_q[q] = active_component.sum(0, 0);
                active_checksum += active_q[q];
            }
        }
        if (ParallelDescriptor::IOProcessor()) {
            amrex::Print() << "ddf_checksum: step=" << step
                           << " lev=" << lev
                           << " valid_sum=" << valid_checksum
                           << " active_sum=" << active_checksum;
            for (int q = 0; q < Q; ++q) {
                amrex::Print() << " active_q" << q << "=" << active_q[q];
            }
            amrex::Print() << '\n';
        }
    }
}

void AmrCoreLBM::PrintLevelDdfChecksum(const char* stage, int lev, bool use_new) {
    GpuArray<Real, Q> component_sum{};
    GpuArray<Real, Q> active_component_sum{};
    Real valid_sum = 0.0;
    Real active_sum = 0.0;

    if (stream_mode == 1) {
        const MultiFab& state = osi_state.at(lev);
        MultiFab& batch = osi_sync_buffer.at(lev);
        const auto phase = osi_phase.at(lev);
        for (int q0 = 0; q0 < Q; q0 += osi_sync_batch_components) {
            const int ncomp = amrex::min(osi_sync_batch_components, Q - q0);
            DecodeOsiValidBatch(state, phase, batch, q0, ncomp);
            for (int n = 0; n < ncomp; ++n) {
                const int q = q0 + n;
                component_sum[q] = batch.sum(n, 0);
                valid_sum += component_sum[q];
                active_component_sum[q] = component_sum[q];
                active_sum += active_component_sum[q];
            }
        }
    } else {
        const MultiFab& state = use_new ? f_new.at(lev) : f_old.at(lev);
        for (int q = 0; q < Q; ++q) {
            component_sum[q] = state.sum(q, 0);
            valid_sum += component_sum[q];

            if (lev < finest_level && cf_mask_mode == 1) {
                MultiFab active(state.boxArray(), state.DistributionMap(), 1, 0,
                                MFInfo().SetArena(The_Arena()));
                for (MFIter mfi(state, false); mfi.isValid(); ++mfi) {
                    const Box bx = mfi.validbox();
                    const auto src = state.const_array(mfi);
                    const auto dst = active.array(mfi);
                    const auto covered = covered_mask.at(lev).const_array(mfi);
                    ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {
                        dst(i, j, k) = (covered(i, j, k) == 0)
                                           ? src(i, j, k, q)
                                           : Real(0.0);
                    });
                }
                active_component_sum[q] = active.sum(0, 0);
            } else {
                active_component_sum[q] = component_sum[q];
            }
            active_sum += active_component_sum[q];
        }
    }

    if (ParallelDescriptor::IOProcessor()) {
        amrex::Print() << "NEW_LEVEL_DIAG stage=" << stage
                       << " mode=" << stream_mode
                       << " lev=" << lev
                       << " array=" << (use_new ? "f_new" : "f_old")
                       << " valid_sum=" << valid_sum
                       << " active_sum=" << active_sum;
        for (int q = 0; q < Q; ++q) {
            amrex::Print() << " q" << q << "=" << component_sum[q]
                           << " active_q" << q << "=" << active_component_sum[q];
        }
        amrex::Print() << '\n';
    }
}

void AmrCoreLBM::PrintInterpolationDiagnostics(const char* stage, int lev) {
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        stream_mode == 0,
        "PrintInterpolationDiagnostics is only available for the A-B path");
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        lev > 0 && lev <= finest_level,
        "PrintInterpolationDiagnostics requires an active fine level");

    const auto& fine_boxes = interp_direct_fine_boxes.at(lev);
    if (fine_boxes.empty()) {
        amrex::Print() << "INTERP_DIAG stage=" << stage
                       << " lev=" << lev << " entries=0\n";
        return;
    }

    const MultiFab& coarse_stage = interp_direct_coarse_stage.at(lev);
    const MultiFab& fine_state = f_old.at(lev);
    const auto& coarse_boxes = coarse_stage.boxArray();
    long long fine_points = 0;
    long long coarse_points = 0;
    for (const Box& box : fine_boxes) {
        fine_points += box.numPts();
    }
    for (int ibox = 0; ibox < coarse_boxes.size(); ++ibox) {
        coarse_points += coarse_boxes[ibox].numPts();
    }

    amrex::Print() << "INTERP_DIAG stage=" << stage
                   << " lev=" << lev
                   << " entries=" << fine_boxes.size()
                   << " fine_points=" << fine_points
                   << " coarse_points=" << coarse_points;
    if (!fine_boxes.empty()) {
        amrex::Print() << " first_fine=" << fine_boxes.front()
                       << " first_coarse=" << coarse_boxes[0]
                       << " last_fine=" << fine_boxes.back()
                       << " last_coarse="
                       << coarse_boxes[coarse_boxes.size() - 1];
    }
    for (int q : {0, 3, 18, 26}) {
        amrex::Print() << " coarse_q" << q << '=' << coarse_stage.norm1(q, 0)
                       << " fine_q" << q << "_valid="
                       << fine_state.norm1(q, 0)
                       << " fine_q" << q << "_grow="
                       << fine_state.norm1(q, fine_state.nGrowVect().max());
    }
    amrex::Print() << '\n';
}

void AmrCoreLBM::PrintRegridDiagnostics() const {
    if (!ParallelDescriptor::IOProcessor()) {
        return;
    }

    amrex::Print() << "REGRID_DIAG finest_level=" << finest_level << '\n';
    for (int lev = 0; lev <= finest_level; ++lev) {
        const auto& ba = boxArray(lev);
        amrex::Print() << "REGRID_DIAG lev=" << lev
                       << " boxes=" << ba.size()
                       << " valid_cells=" << ba.numPts();
        if (lev < max_level && lev < regrid_tag_counts.size() &&
            regrid_tag_counts[lev] >= 0) {
            amrex::Print() << " tagged_to_lev=" << (lev + 1)
                           << " tag_cells=" << regrid_tag_counts[lev];
        }
        amrex::Print() << '\n';
    }

    for (int lev = 0; lev < finest_level; ++lev) {
        if (cf_mask_mode == 0) {
            amrex::Print() << "CF_MASK_DIAG lev=" << lev << " disabled\n";
            continue;
        }

        long fine_cells_per_coarse = 1;
        const auto ratio = refRatio(lev);
        for (int dim = 0; dim < AMREX_SPACEDIM; ++dim) {
            fine_cells_per_coarse *= ratio[dim];
        }
        const auto expected_covered =
            boxArray(lev + 1).numPts() / fine_cells_per_coarse;
        const auto uncovered =
            boxArray(lev).numPts() - covered_cell_counts[lev];
        amrex::Print() << "CF_MASK_DIAG lev=" << lev
                       << " covered=" << covered_cell_counts[lev]
                       << " covered_expected=" << expected_covered
                       << " interface=" << interface_cell_counts[lev]
                       << " uncovered=" << uncovered << '\n';
    }
}

void AmrCoreLBM::PrintParticleChecksums(int step) const {
    if (!particle_checksum) {
        return;
    }

    for (int iparticle = 0; iparticle < particle_num; ++iparticle) {
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            particles[iparticle] != nullptr,
            "verification.particle_checksum requires initialized particles");
        const auto& container = *particles[iparticle];
        using ConstPTDType = LagrangeParticleContainer::ConstPTDType;

        const Long count = container.TotalNumberOfParticles();
        GpuArray<Real, AMREX_SPACEDIM> position_sum{};
        GpuArray<Real, PIdx::nattribs> attribute_sum{};
        for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
            Real value = amrex::ReduceSum(
                container,
                [=] AMREX_GPU_HOST_DEVICE(
                    const ConstPTDType& ptd, int i) noexcept -> Real {
                    return ptd.pos(dir, i);
                });
            ParallelDescriptor::ReduceRealSum(value);
            position_sum[dir] = value;
        }
        for (int n = 0; n < PIdx::nattribs; ++n) {
            Real value = amrex::ReduceSum(
                container,
                [=] AMREX_GPU_HOST_DEVICE(
                    const ConstPTDType& ptd, int i) noexcept -> Real {
                    return ptd.rdata(n)[i];
                });
            ParallelDescriptor::ReduceRealSum(value);
            attribute_sum[n] = value;
        }

        amrex::Print() << std::setprecision(17)
                       << "particle_checksum: step=" << step
                       << " particle=" << iparticle
                       << " count=" << count;
        for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
            amrex::Print() << " pos" << dir << "=" << position_sum[dir];
        }
        for (int n = 0; n < PIdx::nattribs; ++n) {
            amrex::Print() << " attr" << n << "=" << attribute_sum[n];
        }
        amrex::Print() << '\n';
    }
}

void AmrCoreLBM::Boundary(int lev, DdfLayout layout) {
    ScopedPerfTimer timer(perf_stats.boundary);

    const bool use_osi = layout == DdfLayout::Osi;
    const Box domain = Geom(lev).Domain();
    const amrex::IntVect hi{domain.length(0) - 1,
                            domain.length(1) - 1,
                            domain.length(2) - 1};
    const auto is_periodic = Geom(lev).isPeriodicArray();
    const bool has_fine_level = lev < finest_level && cf_mask_mode == 1;
    const std::uint64_t phase = use_osi ? osi_phase.at(lev) : 0;

    amrex::MultiFab& state_lev =
        use_osi ? osi_state.at(lev) : f_new.at(lev);

    for (MFIter mfi(state_lev, false); mfi.isValid(); ++mfi) {
        const Box ring = amrex::grow(mfi.validbox(), state_lev.nGrowVect());
        const auto lo = ring.smallEnd();
        const box3d_osi::FabGeometry fab{
            {lo[0], lo[1], lo[2]},
            {ring.length(0), ring.length(1), ring.length(2)}};
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
                            i, j, k, state, hi, is_periodic, phase, fab);
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

void AmrCoreLBM::DiagnoseBoundaryResult(int lev) {
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        stream_mode == 0,
        "DiagnoseBoundaryResult is only available for the A-B path");
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        lev >= 0 && lev <= finest_level,
        "DiagnoseBoundaryResult requires an active AMR level");
    Gpu::streamSynchronize();
    const Box domain = Geom(lev).Domain();
    const auto is_periodic = Geom(lev).isPeriodicArray();
    const bool has_fine = lev < finest_level && cf_mask_mode == 1;
    const MultiFab& f_new_lev = f_new.at(lev);
    GpuArray<Long, Q> bad{};
    GpuArray<Long, Q> bad_covered{};
    GpuArray<Long, Q> bad_interface{};
    GpuArray<Long, Q> bad_physical{};
    GpuArray<Long, Q> bad_fab_edge{};
    GpuArray<Long, Q> bad_neighbor_nonfinite{};
    GpuArray<Long, Q> bad_active{};
    GpuArray<Long, Q> bad_active_physical{};
    std::array<IntVect, Q> min_iv;
    std::array<IntVect, Q> max_iv;
    std::array<std::vector<IntVect>, Q> samples;
    std::array<std::vector<IntVect>, Q> sample_neighbors;
    std::array<std::vector<IntVect>, Q> active_samples;
    std::array<std::vector<IntVect>, Q> active_physical_samples;

    for (MFIter mfi(f_new_lev, false); mfi.isValid(); ++mfi) {
        const Box bx = mfi.validbox();
        FArrayBox host_state(f_new_lev[mfi].box(), Q,
                             The_Pinned_Arena());
        Gpu::dtoh_memcpy(host_state.dataPtr(),
                         f_new_lev[mfi].dataPtr(),
                         host_state.nBytes());
        IArrayBox host_covered(
            has_fine ? covered_mask[lev][mfi].box() : bx, 1,
            The_Pinned_Arena());
        IArrayBox host_interface(
            has_fine ? interface_mask[lev][mfi].box() : bx, 1,
            The_Pinned_Arena());
        if (has_fine) {
            Gpu::dtoh_memcpy(host_covered.dataPtr(),
                             covered_mask[lev][mfi].dataPtr(),
                             host_covered.nBytes());
            Gpu::dtoh_memcpy(host_interface.dataPtr(),
                             interface_mask[lev][mfi].dataPtr(),
                             host_interface.nBytes());
        }
        const auto state = host_state.const_array();
        const auto covered = host_covered.const_array();
        const auto interface = host_interface.const_array();
        const IntVect lo = bx.smallEnd();
        const IntVect hi_box = bx.bigEnd();
        for (int k = lo[2]; k <= hi_box[2]; ++k) {
            for (int j = lo[1]; j <= hi_box[1]; ++j) {
                for (int i = lo[0]; i <= hi_box[0]; ++i) {
                    const IntVect iv(AMREX_D_DECL(i, j, k));
                    int ni = i;
                    int nj = j;
                    int nk = k;
                    if (!is_periodic[0] && i == domain.smallEnd(0))
                        ni = i + 1;
                    if (!is_periodic[0] && i == domain.bigEnd(0))
                        ni = i - 1;
                    if (!is_periodic[1] && j == domain.smallEnd(1))
                        nj = j + 1;
                    if (!is_periodic[1] && j == domain.bigEnd(1))
                        nj = j - 1;
                    if (!is_periodic[2] && k == domain.smallEnd(2))
                        nk = k + 1;
                    if (!is_periodic[2] && k == domain.bigEnd(2))
                        nk = k - 1;
                    const IntVect neighbor(AMREX_D_DECL(ni, nj, nk));
                    const bool physical = neighbor != iv;
                    const bool fab_edge = i == lo[0] || i == hi_box[0] ||
                                          j == lo[1] || j == hi_box[1] ||
                                          k == lo[2] || k == hi_box[2];
                    bool neighbor_nonfinite = false;
                    if (host_state.box().contains(neighbor)) {
                        for (int nq = 0; nq < Q; ++nq) {
                            neighbor_nonfinite = neighbor_nonfinite ||
                                                 !std::isfinite(state(neighbor, nq));
                        }
                    }
                    for (int q = 0; q < Q; ++q) {
                        if (std::isfinite(state(i, j, k, q)))
                            continue;
                        if (bad[q] == 0) {
                            min_iv[q] = iv;
                            max_iv[q] = iv;
                        } else {
                            min_iv[q].min(iv);
                            max_iv[q].max(iv);
                        }
                        ++bad[q];
                        bad_covered[q] += has_fine && covered(i, j, k) != 0;
                        bad_interface[q] += has_fine && interface(i, j, k) != 0;
                        bad_physical[q] += physical;
                        bad_fab_edge[q] += fab_edge;
                        bad_neighbor_nonfinite[q] += physical && neighbor_nonfinite;
                        const bool active = !has_fine || covered(i, j, k) == 0;
                        bad_active[q] += active;
                        bad_active_physical[q] += active && physical;
                        if (active && active_samples[q].size() < 12) {
                            active_samples[q].push_back(iv);
                        }
                        if (active && physical &&
                            active_physical_samples[q].size() < 12) {
                            active_physical_samples[q].push_back(iv);
                        }
                        if (samples[q].size() < 8) {
                            samples[q].push_back(iv);
                            sample_neighbors[q].push_back(neighbor);
                        }
                    }
                }
            }
        }
    }
    for (int q = 0; q < Q; ++q) {
        if (bad[q] == 0)
            continue;
        amrex::Print() << "BOUNDARY_NONFINITE lev=" << lev << " q=" << q
                       << " count=" << bad[q]
                       << " covered=" << bad_covered[q]
                       << " interface=" << bad_interface[q]
                       << " physical_boundary=" << bad_physical[q]
                       << " fab_edge=" << bad_fab_edge[q]
                       << " active=" << bad_active[q]
                       << " active_physical_boundary="
                       << bad_active_physical[q]
                       << " boundary_neighbor_nonfinite="
                       << bad_neighbor_nonfinite[q]
                       << " bbox=" << Box(min_iv[q], max_iv[q]);
        for (std::size_t n = 0; n < samples[q].size(); ++n) {
            amrex::Print() << " sample" << n << "=" << samples[q][n]
                           << " neighbor" << n << "="
                           << sample_neighbors[q][n];
        }
        for (std::size_t n = 0; n < active_samples[q].size(); ++n) {
            amrex::Print() << " active_sample" << n << "="
                           << active_samples[q][n];
        }
        for (std::size_t n = 0; n < active_physical_samples[q].size(); ++n) {
            amrex::Print() << " active_physical_sample" << n << "="
                           << active_physical_samples[q][n];
        }
        amrex::Print() << '\n';
    }
}

void AmrCoreLBM::Collide(int lev, int n, DdfLayout layout) {
    ScopedPerfTimer timer(perf_stats.collide);

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

    if (use_osi) {
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            collide_mode == 1,
            "OSI collision requires lbm.collide_mode=1");
        for (int d = 0; d < AMREX_SPACEDIM; ++d) {
            AMREX_ALWAYS_ASSERT(
                state_lev.nGrowVect()[d] <= nghost);
        }
    } else {
        AMREX_ALWAYS_ASSERT(n <= nghost);
    }

    const bool use_tiling = use_osi ? false : TilingIfNotGPU();
    for (MFIter mfi(state_lev, use_tiling); mfi.isValid(); ++mfi) {
        // 与 grown-Fab OSI 保持一致：周期域外 ghost 参与碰撞，非周期
        // ghost 仍由物理边界条件负责，不进入碰撞 kernel。
        const Box ring = amrex::grow(mfi.validbox(), state_lev.nGrowVect());
        const Box bx = (use_osi ? ring : mfi.growntilebox(n)) &
                       collision_domain;
        const auto lo = ring.smallEnd();
        const box3d_osi::FabGeometry fab{
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
            amrex::ParallelFor(
                bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                    if (has_fine_level && covered(i, j, k) != 0 &&
                        interface(i, j, k) == 0) {
                        return;
                    }
                    collide_bgk_register_osi(
                        i, j, k, state, phase, fab, omega_lev);
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

void AmrCoreLBM::DiagnoseStreamResult(int lev, int n) {
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        stream_mode == 0,
        "DiagnoseStreamResult is only available for the A-B path");
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        lev >= 0 && lev <= finest_level,
        "DiagnoseStreamResult requires an active AMR level");
    AMREX_ALWAYS_ASSERT(n >= 1);
    AMREX_ALWAYS_ASSERT(n - 1 <= 1);

    const MultiFab& f_old_lev = f_old.at(lev);
    const MultiFab& f_new_lev = f_new.at(lev);
    const bool has_fine_level = lev < finest_level;
    Long launch_cells = 0;
    for (MFIter mfi(f_old_lev, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        launch_cells += mfi.growntilebox(n - 1).numPts();
    }

    amrex::Print() << "STREAM_INPUT_NORM lev=" << lev;
    for (int q : {0, 3, 18, 26}) {
        amrex::Print() << " q" << q << "_valid=" << f_old_lev.norm1(q, 0)
                       << " q" << q << "_grow="
                       << f_old_lev.norm1(q, n - 1);
    }
    amrex::Print() << '\n';

    Gpu::streamSynchronize();
    const Box domain = Geom(lev).Domain();
    GpuArray<Long, Q> bad{};
    GpuArray<Long, Q> bad_covered{};
    GpuArray<Long, Q> bad_interface{};
    GpuArray<Long, Q> bad_physical{};
    GpuArray<Long, Q> bad_fab_edge{};
    GpuArray<Long, Q> bad_source{};
    GpuArray<Long, Q> source_ghost{};
    GpuArray<Long, Q> source_outside_fab{};
    std::array<IntVect, Q> first_iv{};
    std::array<IntVect, Q> first_src{};
    std::array<int, Q> first_has{};
    constexpr int ex[Q] = {0, 0, 0, -1, 1, 0, 0, -1, 1, -1, 1, 0, 0, -1, 1, 0, 0, -1, 1, 1, -1, 1, -1, 1, -1, 1, -1};
    constexpr int ey[Q] = {0, 1, -1, 0, 0, 0, 0, 1, 1, -1, -1, 1, -1, 0, 0, 1, -1, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1};
    constexpr int ez[Q] = {0, 0, 0, 0, 0, 1, -1, 0, 0, 0, 0, 1, 1, 1, 1, -1, -1, -1, -1, 1, 1, 1, 1, -1, -1, -1, -1};
    for (MFIter mfi(f_new_lev, false); mfi.isValid(); ++mfi) {
        const Box bx = mfi.validbox();
        FArrayBox host_new(f_new_lev[mfi].box(), Q, The_Pinned_Arena());
        Gpu::dtoh_memcpy(host_new.dataPtr(), f_new_lev[mfi].dataPtr(),
                         host_new.nBytes());
        const Box source_box = f_old_lev[mfi].box();
        FArrayBox host_old(source_box, Q, The_Pinned_Arena());
        Gpu::dtoh_memcpy(host_old.dataPtr(), f_old_lev[mfi].dataPtr(),
                         host_old.nBytes());
        const bool has_masks = has_fine_level && cf_mask_mode == 1;
        IArrayBox host_covered(
            has_masks ? covered_mask[lev][mfi].box() : bx, 1,
            The_Pinned_Arena());
        IArrayBox host_interface(
            has_masks ? interface_mask[lev][mfi].box() : bx, 1,
            The_Pinned_Arena());
        if (has_masks) {
            Gpu::dtoh_memcpy(host_covered.dataPtr(),
                             covered_mask[lev][mfi].dataPtr(),
                             host_covered.nBytes());
            Gpu::dtoh_memcpy(host_interface.dataPtr(),
                             interface_mask[lev][mfi].dataPtr(),
                             host_interface.nBytes());
        }
        const auto values = host_new.const_array();
        const auto sources = host_old.const_array();
        const auto covered = host_covered.const_array();
        const auto interface = host_interface.const_array();
        const IntVect lo = bx.smallEnd();
        const IntVect hi = bx.bigEnd();
        for (int k = lo[2]; k <= hi[2]; ++k) {
            for (int j = lo[1]; j <= hi[1]; ++j) {
                for (int i = lo[0]; i <= hi[0]; ++i) {
                    const IntVect iv(AMREX_D_DECL(i, j, k));
                    const bool phys =
                        (!Geom(lev).isPeriodic(0) &&
                         (i == domain.smallEnd(0) || i == domain.bigEnd(0))) ||
                        (!Geom(lev).isPeriodic(1) &&
                         (j == domain.smallEnd(1) || j == domain.bigEnd(1))) ||
                        (!Geom(lev).isPeriodic(2) &&
                         (k == domain.smallEnd(2) || k == domain.bigEnd(2)));
                    const bool edge = i == lo[0] || i == hi[0] ||
                                      j == lo[1] || j == hi[1] ||
                                      k == lo[2] || k == hi[2];
                    for (int q = 0; q < Q; ++q) {
                        if (std::isfinite(values(i, j, k, q)))
                            continue;
                        ++bad[q];
                        const bool cov = has_fine_level && cf_mask_mode == 1 &&
                                         covered(i, j, k) != 0;
                        const bool intf = has_fine_level && cf_mask_mode == 1 &&
                                          interface(i, j, k) != 0;
                        bad_covered[q] += cov;
                        bad_interface[q] += intf;
                        bad_physical[q] += phys;
                        bad_fab_edge[q] += edge;
                        const IntVect src_iv = iv -
                                               IntVect(AMREX_D_DECL(ex[q], ey[q], ez[q]));
                        const bool src_in_valid = bx.contains(src_iv);
                        const bool src_in_fab = source_box.contains(src_iv);
                        source_ghost[q] += !src_in_valid;
                        source_outside_fab[q] += !src_in_fab;
                        if (src_in_fab && !std::isfinite(sources(src_iv, q))) {
                            ++bad_source[q];
                        }
                        if (!first_has[q]) {
                            first_has[q] = 1;
                            first_iv[q] = iv;
                            first_src[q] = iv - IntVect(AMREX_D_DECL(ex[q], ey[q], ez[q]));
                        }
                    }
                }
            }
        }
    }
    for (int q = 0; q < Q; ++q) {
        if (bad[q] != 0) {
            amrex::Print() << "STREAM_NONFINITE lev=" << lev << " q=" << q
                           << " count=" << bad[q]
                           << " covered=" << bad_covered[q]
                           << " interface=" << bad_interface[q]
                           << " physical_boundary=" << bad_physical[q]
                           << " fab_edge=" << bad_fab_edge[q]
                           << " source_nonfinite=" << bad_source[q]
                           << " source_ghost=" << source_ghost[q]
                           << " source_outside_fab=" << source_outside_fab[q]
                           << " first_iv=" << first_iv[q]
                           << " first_pull_src=" << first_src[q] << '\n';
        }
    }
    if (ParallelDescriptor::IOProcessor()) {
        Long covered_cells = (has_fine_level && cf_mask_mode == 1) ? covered_mask[lev].sum(0, 0) : 0;
        amrex::Print() << "STREAM_RANGE lev=" << lev << " n=" << n << " fabs=" << f_old_lev.boxArray().size()
                       << " launch_cells=" << launch_cells << " covered_cells=" << covered_cells
                       << " stream_cells=" << (launch_cells - covered_cells) << '\n';
        for (int ib = 0; ib < f_old_lev.boxArray().size(); ++ib) {
            amrex::Print() << "STREAM_FAB lev=" << lev << " fab=" << ib << " box=" << f_old_lev.boxArray()[ib] << '\n';
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

//********************************************************************//
//                           ibm  function                            //
//********************************************************************//
void AmrCoreLBM::InitParticle(int lev) {
    for (int i = 0; i < particle_num; i++) {
        particles[i] = std::make_unique<LagrangeParticleContainer>(this, points[i], i, grid_);
        particles[i]->InitParticle(lev);
        // particles[i] ->InitParticleFromFile(lev, "../../../object/sphere128");
        // particles[i] ->InitParticleFromFile(lev, "../../../object/car_fine");
    }
}

void AmrCoreLBM::InterpForce(int lev) {
    amrex::MultiFab& rho_lev = density[lev];
    amrex::MultiFab& u_lev = velocity[lev];
    amrex::MultiFab& force_lev = force[lev];

    force_lev.setVal(0.0, nghost);
    for (int i = 0; i < particle_num; i++) {
        particles[i]->InterpForce(lev, rho_lev, u_lev, force_lev);
        // particles[i]->InterpForceWallModel(lev, rho_lev, u_lev, force_lev);
    }
}

void AmrCoreLBM::SumForce(int lev) {
    MultiFab* mf_pointer = &force[lev];

    mf_pointer->SumBoundary(Geom(lev).periodicity());
}

void AmrCoreLBM::ComputeParticle(int lev) {
    CommunicateLevel(lev);
    ComputeMacroLevel(lev);
    InterpForce(lev);
    SumForce(lev);
}

void AmrCoreLBM::ReduceFxy(int lev, int step) {
    for (int i = 0; i < particle_num; i++) {
        particles[i]->SaveFxy(lev, step);
    }
}

void AmrCoreLBM::SaveParticleVelocity(int lev, int step) {
    for (int i = 0; i < particle_num; i++) {
        particles[i]->SaveVelocity(lev, step);
    }
}

void AmrCoreLBM::SaveParticlePosition(int lev, int step) {
    for (int i = 0; i < particle_num; i++) {
        particles[i]->SavePosition(lev, step);
    }
}

void AmrCoreLBM::SaveParticleDistance(int lev, int step) {
    if (ParallelDescriptor::MyProc() == ParallelDescriptor::IOProcessorNumber()) {
        std::string filename = "dist.dat";
        std::ofstream file(filename, std::ios::app);

        if (!file.is_open()) {
            std::cerr << "Cannot open the file: " << filename << std::endl;
            std::exit(1); // 错误退出
        }

        amrex::Real lx = points[0][0] - points[1][0];
        amrex::Real ly = points[0][1] - points[1][1];
        amrex::Real lz = points[0][2] - points[1][2];

        amrex::Real dist = std::sqrt(lx * lx + ly * ly + lz * lz) / D - 1.0;

        file << step << "\t" << dist << "\n";
        file.close();
    }
}

void AmrCoreLBM::PrintParticleParm() {
    particles[0]->PrintParticleParm();
}

void AmrCoreLBM::RedistributeParticle() {
    for (int i = 0; i < particle_num; i++) {
        particles[i]->Redistribute();
    }
}

void AmrCoreLBM::InitCpPoint(int lev) {
    for (int i = 0; i < particle_num; i++) {
        particlesCp[i] = std::make_unique<AuxiliaryPointContainer>(
            this, points[i], i, grid_);
        particlesCp[i]->InitCpPoint(lev);
    }
}

void AmrCoreLBM::ComputeCp(int lev, int step) {
    CommunicateLevel(lev);
    ComputeMacroLevel(lev);

    amrex::MultiFab& rho_lev = density[lev];

    for (int i = 0; i < particle_num; i++) {
        particlesCp[i]->InterpCp(lev, rho_lev);
        particlesCp[i]->WriteCp(step);
    }
}

void AmrCoreLBM::LubForceParticle(int lev, amrex::Real cur_time) {
    // 在这里遍历，然后传入两个颗粒
    for (int p1 = 0; p1 < particle_num; p1++) {
        for (int p2 = p1 + 1; p2 < particle_num; p2++) {
            particles[p1]->CollideParticle(particles[p2]);
        }
    }

    for (int p1 = 0; p1 < particle_num; p1++) {
        particles[p1]->CollideWall();
    }
}

void AmrCoreLBM::MoveParticle(int lev, amrex::Real cur_time) {
    for (int i = 0; i < particle_num; i++) {
        particles[i]->MoveParticle(lev, cur_time);
        particles[i]->Redistribute();
    }
}

//********************************************************************//
//                     Pure virtual function                          //
//********************************************************************//
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

        FillCoarsePatch(lev, time, f_old_lev);
    } else {
        InitializeOsiLevel(lev, ba, dm);
        amrex::MultiFab& state = osi_state.at(lev);
        state.setVal(std::numeric_limits<Real>::quiet_NaN());

        FillNewLevelFromCoarse(lev, time);
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

        osi_decode_tags.at(lev).undefine();
        osi_encode_tags.at(lev).undefine();

        MultiFab old_state;
        MultiFab old_decode_batch;

        std::swap(old_state, osi_state.at(lev));
        std::swap(old_decode_batch, osi_sync_buffer.at(lev));

        const auto old_phase = osi_phase.at(lev);

        InitializeOsiLevel(lev, ba, dm);

        MultiFab& new_osi_state = osi_state.at(lev);
        new_osi_state.setVal(std::numeric_limits<Real>::quiet_NaN());

        // 旧、新 fine 布局重叠区：按旧 phase 分批解码，再由 ParallelCopy
        // 完成本地或跨 rank remap；新布局从 phase=0 开始。
        for (int q0 = 0; q0 < Q; q0 += osi_sync_batch_components) {
            const int ncomp =
                amrex::min(osi_sync_batch_components, Q - q0);
            DecodeOsiValidBatch(
                old_state, old_phase, old_decode_batch, q0, ncomp);
            new_osi_state.ParallelCopy(
                old_decode_batch, 0, q0, ncomp, IntVect(0), IntVect(0),
                Geom(lev).periodicity());
        }

        if (lev > 0) {
            const IntVect fill_ng(0);
            const auto ratio = refRatio(lev - 1);
            const auto coarsener = DdfInterpolater()->BoxCoarsener(ratio);
            const auto& fpc = FabArrayBase::TheFPinfo(
                old_state, new_osi_state, fill_ng, coarsener,
                Geom(lev), Geom(lev - 1), nullptr); // 得到新 fine 中不被旧 fine 覆盖的区域以及为了插值这些 fine 区域需要读取的粗层 stencil
            FillOsiFinePatchFromCoarse(
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
    osi_sync_buffer.at(lev).define(ba, dm, osi_sync_batch_components, nghost);
    osi_phase.at(lev) = 0;
    BuildOsiCommunicationRegionCache(lev);
}

void AmrCoreLBM::FillNewLevelFromCoarse(int lev, Real time) {
    AMREX_ALWAYS_ASSERT(lev > 0);
    const bool use_osi = stream_mode == 1;
    if (use_osi) {
        AMREX_ALWAYS_ASSERT(osi_phase.at(lev) == 0);
    }

    MultiFab& fine_state =
        use_osi ? osi_state.at(lev) : f_old.at(lev);
    const MultiFab& coarse_state =
        use_osi ? osi_state.at(lev - 1) : f_old.at(lev - 1);
    MultiFab local_batch;
    MultiFab* coarse_batch = nullptr;
    if (use_osi) {
        coarse_batch = &osi_sync_buffer.at(lev - 1);
    } else {
        local_batch.define(coarse_state.boxArray(),
                           coarse_state.DistributionMap(),
                           osi_sync_batch_components, nghost);
        coarse_batch = &local_batch;
    }

    ComputeMacroLevel(lev - 1);
    const MultiFab& coarse_density = density.at(lev - 1);
    const MultiFab& coarse_velocity = velocity.at(lev - 1);
    const Real scale = tau.at(lev) / tau.at(lev - 1) / Real(2.0);

    // 两种存储模式共享宏观量刷新、非平衡缩放和空间插值；区别只在于
    // A-B 直接复制 coarse batch，而 OSI 先按当前 phase 解码 batch。
    for (int q0 = 0; q0 < Q; q0 += osi_sync_batch_components) {
        const int ncomp = amrex::min(osi_sync_batch_components, Q - q0);
        if (use_osi) {
            DecodeOsiValidBatch(coarse_state, osi_phase.at(lev - 1),
                                *coarse_batch, q0, ncomp);
        } else {
            MultiFab::Copy(*coarse_batch, coarse_state,
                           q0, 0, ncomp, 0);
        }
        ScaleCanonicalBatch(coarse_density, coarse_velocity,
                            *coarse_batch, q0, ncomp, scale);
        InterpolateNewFineBatch(
            lev, time, fine_state, *coarse_batch, q0, ncomp);
    }
}

void AmrCoreLBM::InterpolateNewFineBatch(
    int lev, Real time, MultiFab& fine, MultiFab& coarse_batch,
    int q0, int ncomp) {
    if (Gpu::inLaunchRegion()) {
        GpuBndryFuncFab<AmrCoreFill> gpu_bndry_func(AmrCoreFill{});
        PhysBCFunct<GpuBndryFuncFab<AmrCoreFill>> cphysbc(
            geom[lev - 1], bcs, gpu_bndry_func);
        PhysBCFunct<GpuBndryFuncFab<AmrCoreFill>> fphysbc(
            geom[lev], bcs, gpu_bndry_func);
        amrex::InterpFromCoarseLevel(
            fine, time, coarse_batch, 0, q0, ncomp,
            geom[lev - 1], geom[lev], cphysbc, q0, fphysbc, q0,
            refRatio(lev - 1), DdfInterpolater(), bcs, q0);
    } else {
        CpuBndryFuncFab bndry_func(nullptr);
        PhysBCFunct<CpuBndryFuncFab> cphysbc(
            geom[lev - 1], bcs, bndry_func);
        PhysBCFunct<CpuBndryFuncFab> fphysbc(
            geom[lev], bcs, bndry_func);
        amrex::InterpFromCoarseLevel(
            fine, time, coarse_batch, 0, q0, ncomp,
            geom[lev - 1], geom[lev], cphysbc, q0, fphysbc, q0,
            refRatio(lev - 1), DdfInterpolater(), bcs, q0);
    }
}

void AmrCoreLBM::ScaleCanonicalBatch(
    const MultiFab& density_mf, const MultiFab& velocity_mf,
    MultiFab& batch, int q0, int ncomp, Real scale) const {
    AMREX_ALWAYS_ASSERT(density_mf.boxArray() == batch.boxArray());
    AMREX_ALWAYS_ASSERT(velocity_mf.boxArray() == batch.boxArray());
    AMREX_ALWAYS_ASSERT(density_mf.DistributionMap() == batch.DistributionMap());
    AMREX_ALWAYS_ASSERT(velocity_mf.DistributionMap() == batch.DistributionMap());
    AMREX_ALWAYS_ASSERT(q0 >= 0 && q0 + ncomp <= Q);
    AMREX_ALWAYS_ASSERT(ncomp <= batch.nComp());

    for (MFIter mfi(batch, false); mfi.isValid(); ++mfi) {
        const Box bx = mfi.validbox();
        const auto rho_field = density_mf.const_array(mfi);
        const auto velocity_field = velocity_mf.const_array(mfi);
        const auto ddf = batch.array(mfi);
        amrex::ParallelFor(
            bx, ncomp,
            [=] AMREX_GPU_DEVICE(int i, int j, int k, int n) noexcept {
                const int q = q0 + n;
                const Real rho = rho_field(i, j, k);
                const Real ux = velocity_field(i, j, k, 0);
                const Real uy = velocity_field(i, j, k, 1);
                const Real uz = velocity_field(i, j, k, 2);
                const Real feq = feqQian(rho, {ux, uy, uz}, q);
                ddf(i, j, k, n) =
                    feq + (ddf(i, j, k, n) - feq) * scale;
            });
    }
}

void AmrCoreLBM::DecodeOsiValidBatch(
    const MultiFab& state, std::uint64_t phase, MultiFab& batch,
    int q0, int ncomp) const {
    AMREX_ALWAYS_ASSERT(state.boxArray() == batch.boxArray());
    AMREX_ALWAYS_ASSERT(state.DistributionMap() == batch.DistributionMap());
    AMREX_ALWAYS_ASSERT(q0 >= 0 && q0 + ncomp <= Q);
    AMREX_ALWAYS_ASSERT(ncomp <= batch.nComp());

    for (MFIter mfi(state, false); mfi.isValid(); ++mfi) {
        const Box bx = mfi.validbox();
        const Box ring = amrex::grow(bx, state.nGrowVect());
        const auto lo = ring.smallEnd();
        const box3d_osi::FabGeometry fab{
            {lo[0], lo[1], lo[2]},
            {ring.length(0), ring.length(1), ring.length(2)}};
        const auto src = state.const_array(mfi);
        const auto dst = batch.array(mfi);
        amrex::ParallelFor(
            bx, ncomp,
            [=] AMREX_GPU_DEVICE(int i, int j, int k, int n) noexcept {
                const int q = q0 + n;
                const auto raw = box3d_osi::osi_address(
                    {i, j, k}, {e[q][0], e[q][1], e[q][2]}, phase,
                    fab);
                dst(i, j, k, n) = src(raw.x, raw.y, raw.z, q);
            });
    }
}

void AmrCoreLBM::FillOsiFinePatchFromCoarse(
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
    MultiFab& decode_batch = osi_sync_buffer.at(lev - 1);
    const auto coarse_phase = osi_phase.at(lev - 1);
    for (int q0 = 0; q0 < Q; q0 += osi_sync_batch_components) {
        const int ncomp =
            amrex::min(osi_sync_batch_components, Q - q0);
        DecodeOsiValidBatch(
            coarse_state, coarse_phase, decode_batch, q0, ncomp);
        coarse_patch.ParallelCopy(
            decode_batch, 0, q0, ncomp, IntVect(0), IntVect(0),
            Geom(lev - 1).periodicity());
    }

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
void AmrCoreLBM::ClearLevel(int lev) {
    osi_decode_tags[lev].undefine();
    osi_encode_tags[lev].undefine();
    f_old[lev].clear();
    f_new[lev].clear();
    osi_state[lev].clear();
    osi_sync_buffer[lev].clear();
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
    const bool allocate_ab = stream_mode == 0 || osi_ab_check;
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

        if (osi_ab_check) {
            // Oracle 与 OSI 从完全相同的 logical valid 初值出发；ghost 由首次通信填充。
            amrex::MultiFab::Copy(f_old.at(lev), state, 0, 0, Q, 0);
            amrex::MultiFab::Copy(f_new.at(lev), state, 0, 0, Q, 0);
        }
        // 这里只初始化 valid。首次碰撞所需 ghost 由 main/Cycle2 通过
        // FillGhostLevel 建立，避免把 ghost 生命周期混入网格构造回调。
    }
}

/*************************************第3版*ErrorEst***************************************/
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

void AmrCoreLBM::WriteCheckpoint(int step, amrex::Real time) const {
    const std::string level_prefix = "Level_";
    const bool isIOP = ParallelDescriptor::IOProcessor();
    const std::string out_chkname = amrex::Concatenate(params_.chk_prefix, step, 8);
    const bool write_particles = params_.write_particles;

    if (isIOP) {
        amrex::Print() << "[Checkpoint] Writing '" << out_chkname
                       << "' step=" << step << " time=" << time << '\n';

        amrex::UtilCreateCleanDirectory(out_chkname, false); // 为当前 checkpoint 创建一个“干净的输出目录”。

        for (int lev = 0; lev <= finest_level; ++lev) { // 为当前 checkpoint 创建各个 AMR 层级对应的子目录。
            const std::string level_dir = out_chkname + "/" + level_prefix + std::to_string(lev);
            amrex::UtilCreateDirectory(level_dir, 0755);
        }

        if (write_particles) {
            for (int i = 0; i < particle_num; ++i) {
                const std::string pdir = out_chkname + "/particles_" + std::to_string(i);
                amrex::UtilCreateDirectory(pdir, 0755);
            }
        }
    }
    ParallelDescriptor::Barrier();

    if (isIOP) { // 写Header
        std::ofstream header(out_chkname + "/Header", std::ios::out | std::ios::trunc);
        if (!header.is_open()) {
            amrex::Print() << "[Checkpoint][ERROR] Cannot open '" << out_chkname
                           << "/Header' for writing. Abort checkpoint write for this step.\n";
            return;
        }
        header.precision(17);
        header << checkpoint_label_v2 << '\n';
        header << (stream_mode == 1 ? checkpoint_layout_osi
                                    : checkpoint_layout_ab)
               << '\n';
        header << step << '\n';
        header << time << '\n';
        header << finest_level << '\n';
        for (int lev = 0; lev <= finest_level; ++lev) {
            boxArray(lev).writeOn(header);
            header << '\n';
            DistributionMap(lev).writeOn(header);
            header << '\n';
        }
        header.flush(); // 将 C++ 输出缓冲区中的数据立即刷新到文件中
    }

    for (int lev = 0; lev <= finest_level; ++lev) {
        const std::string old_name = MultiFabFileFullPrefix(
            lev, out_chkname, level_prefix, "f_old");
        if (stream_mode == 0) {
            VisMF::Write(f_old[lev], old_name);
            continue;
        }

        // 保存检查点时不写入 OSI 原始坐标、Fab 局部相位；重启作业时允许使用完全不同的进程网格分配策略，不需要和生成 checkpoint 时的`DistributionMapping`保持一致。
        const MultiFab& state = osi_state.at(lev);
        MultiFab canonical(
            state.boxArray(), state.DistributionMap(), Q, 0);
        const std::uint64_t phase = osi_phase.at(lev);
        for (MFIter mfi(state, false); mfi.isValid(); ++mfi) {
            const Box bx = mfi.validbox();
            const Box ring = amrex::grow(bx, state.nGrowVect());
            const auto lo = ring.smallEnd();
            const box3d_osi::FabGeometry fab{
                {lo[0], lo[1], lo[2]},
                {ring.length(0), ring.length(1), ring.length(2)}};
            const auto src = state.const_array(mfi);
            const auto dst = canonical.array(mfi);
            amrex::ParallelFor(
                bx, Q,
                [=] AMREX_GPU_DEVICE(
                    int i, int j, int k, int q) noexcept {
                    const auto raw = box3d_osi::osi_address(
                        {i, j, k},
                        {e[q][0], e[q][1], e[q][2]}, phase, fab);
                    dst(i, j, k, q) = src(raw.x, raw.y, raw.z, q);
                });
        }
        VisMF::Write(canonical, old_name);
    }

    if (!write_particles) {
        if (isIOP) {
            amrex::Print() << "[Checkpoint] Skip particle containers (checkpoint.write_particles=0)\n";
        }
    } else {
        for (int i = 0; i < particle_num; ++i) {
            if (particles[i]) {
                const std::string pname = "particles_" + std::to_string(i);
                particles[i]->Checkpoint(out_chkname, pname);
            }
        }
    }
    ParallelDescriptor::Barrier();

    if (params_.keep_latest_only && isIOP) {
        DIR* dir = opendir("."); // 打开当前工作目录
        if (dir) {
            struct dirent* entry;
            int removed_count = 0;
            const auto is_chk_dir = [&](const std::string& name) {
                if (name == out_chkname) {
                    return false;
                }
                if (name.rfind(params_.chk_prefix, 0) != 0) { // 检查前缀
                    return false;
                }
                if (name.size() <= params_.chk_prefix.size()) { // 确保前缀后面存在内容
                    return false;
                }
                return std::all_of(name.begin() + params_.chk_prefix.size(), name.end(),
                                   [](unsigned char ch) { return std::isdigit(ch); }); // 只有当前缀后面的所有字符都是数字时，才判定为 checkpoint。
            };

            while ((entry = readdir(dir)) != nullptr) { // 遍历当前目录
                std::string name(entry->d_name);
                if (is_chk_dir(name)) {
                    amrex::Print() << "[Checkpoint] Removing old checkpoint directory '" << name << "'\n";
                    std::string cmd = "rm -rf '" + name + "'";
                    int rc = std::system(cmd.c_str());
                    if (rc != 0) {
                        amrex::Print() << "[Checkpoint][WARN] Failed to remove '" << name
                                       << "' rc=" << rc << '\n';
                    } else {
                        ++removed_count;
                    }
                }
            }
            closedir(dir);
            amrex::Print() << "[Checkpoint] Old checkpoint cleanup done. removed=" << removed_count << "\n";
        } else { // 目录无法打开时
            amrex::Print() << "[Checkpoint][WARN] Could not open current directory for deleting old checkpoints\n";
        }
    }
}

void AmrCoreLBM::CompareDdfCheckpoint(
    const std::string& checkpoint_path) const {
    const std::string header_file = checkpoint_path + "/Header";
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        amrex::FileExists(header_file),
        "DDF reference checkpoint is missing Header: " + header_file);

    amrex::Vector<char> header_chars;
    ParallelDescriptor::ReadAndBcastFile(header_file, header_chars);
    std::istringstream header{std::string(header_chars.dataPtr())};

    std::string label;
    std::getline(header, label);
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        label == checkpoint_label_v1 || label == checkpoint_label_v2,
        "Invalid DDF reference checkpoint: " + checkpoint_path);

    std::string line;
    if (label == checkpoint_label_v2) {
        std::getline(header, line); // canonical layout identifier
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            line == checkpoint_layout_ab || line == checkpoint_layout_osi,
            "Unknown DDF checkpoint layout: " + line);
    }
    std::getline(header, line); // step
    std::getline(header, line); // time
    int reference_finest = -1;
    header >> reference_finest;
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        reference_finest == finest_level,
        "DDF comparison requires identical finest levels");

    constexpr const char* level_prefix = "Level_";
    Real global_linf = 0.0;
    Real global_l1 = 0.0;
    Real global_diff_l2_sq = 0.0;
    Real global_ref_l2_sq = 0.0;

    amrex::Print() << std::setprecision(17)
                   << "ddf_norm_begin: reference=" << checkpoint_path
                   << " finest_level=" << finest_level
                   << " ncomp=" << Q << '\n';

    for (int lev = 0; lev <= finest_level; ++lev) {
        const std::string reference_name = MultiFabFileFullPrefix(
            lev, checkpoint_path, level_prefix, "f_old");
        MultiFab reference;
        VisMF::Read(reference, reference_name);

        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            reference.nComp() == Q,
            "DDF comparison found a reference component-count mismatch");
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            reference.boxArray() == boxArray(lev),
            "DDF comparison requires identical BoxArrays at every level");

        MultiFab reference_on_current(
            boxArray(lev), DistributionMap(lev), Q, 0);
        reference_on_current.ParallelCopy(
            reference, 0, 0, Q, IntVect(0), IntVect(0));

        if (stream_mode == 1) {
            const MultiFab& state = osi_state.at(lev);
            MultiFab difference_batch(
                state.boxArray(), state.DistributionMap(),
                osi_sync_batch_components, 0);
            const bool has_fine =
                lev < finest_level && cf_mask_mode == 1;
            const Long valid_cells = state.boxArray().numPts();
            const Long active_cells =
                has_fine ? valid_cells - covered_cell_counts.at(lev)
                         : valid_cells;
            Real level_valid_linf = 0.0;
            Real level_valid_l1 = 0.0;
            Real level_valid_l2_sq = 0.0;
            Real level_active_linf = 0.0;
            Real level_active_l1 = 0.0;
            Real level_active_l2_sq = 0.0;
            Real level_active_ref_l2_sq = 0.0;

            for (int q0 = 0; q0 < Q;
                 q0 += osi_sync_batch_components) {
                const int ncomp = amrex::min(
                    osi_sync_batch_components, Q - q0);
                DecodeOsiValidBatch(
                    state, osi_phase.at(lev), difference_batch, q0,
                    ncomp);

                for (MFIter mfi(difference_batch, false);
                     mfi.isValid(); ++mfi) {
                    const Box bx = mfi.validbox();
                    const auto diff = difference_batch.array(mfi);
                    const auto ref = reference_on_current.const_array(mfi);
                    amrex::ParallelFor(
                        bx, ncomp,
                        [=] AMREX_GPU_DEVICE(
                            int i, int j, int k, int n) noexcept {
                            diff(i, j, k, n) -=
                                ref(i, j, k, q0 + n);
                        });
                }

                for (int n = 0; n < ncomp; ++n) {
                    const int q = q0 + n;
                    const Real valid_linf =
                        difference_batch.norminf(n);
                    const Real valid_l1 =
                        difference_batch.norm1(n);
                    const Real valid_l2 =
                        difference_batch.norm2(n);

                    // Coordinate-level classification of the largest valid-cell
                    // mismatch.  This is intentionally done before covered-cell
                    // masking so that covered/interface status is reported too.
                    Real max_diff = 0.0;
                    IntVect max_iv(0);
                    int max_covered = 0;
                    int max_interface = 0;
                    int max_physical = 0;
                    int max_fab_edge = 0;
                    Long mismatch_count = 0;
                    Long mismatch_covered = 0;
                    Long mismatch_interface = 0;
                    Long mismatch_physical = 0;
                    Long mismatch_fab_edge = 0;
                    const Box domain = Geom(lev).Domain();
                    for (MFIter mfi(difference_batch, false);
                         mfi.isValid(); ++mfi) {
                        const Box bx = mfi.validbox();
                        FArrayBox host_diff(mfi.validbox(), difference_batch.nComp(), The_Pinned_Arena());
                        Gpu::dtoh_memcpy(host_diff.dataPtr(), difference_batch[mfi].dataPtr(), host_diff.nBytes());
                        const auto diff = host_diff.const_array();
                        const auto covered = has_fine
                                                 ? covered_mask.at(lev).const_array(mfi)
                                                 : Array4<const int>{};
                        const auto interface = has_fine
                                                   ? interface_mask.at(lev).const_array(mfi)
                                                   : Array4<const int>{};
                        const IntVect lo = bx.smallEnd();
                        const IntVect hi = bx.bigEnd();
                        for (int k = lo[2]; k <= hi[2]; ++k) {
                            for (int j = lo[1]; j <= hi[1]; ++j) {
                                for (int i = lo[0]; i <= hi[0]; ++i) {
                                    const Real d = std::abs(diff(i, j, k, n));
                                    if (d <= Real(1.e-12))
                                        continue;
                                    ++mismatch_count;
                                    const int cv = has_fine ? covered(i, j, k) : 0;
                                    const int iv = has_fine ? interface(i, j, k) : 0;
                                    const bool physical =
                                        (!Geom(lev).isPeriodic(0) &&
                                         (i == domain.smallEnd(0) || i == domain.bigEnd(0))) ||
                                        (!Geom(lev).isPeriodic(1) &&
                                         (j == domain.smallEnd(1) || j == domain.bigEnd(1))) ||
                                        (!Geom(lev).isPeriodic(2) &&
                                         (k == domain.smallEnd(2) || k == domain.bigEnd(2)));
                                    const bool fab_edge =
                                        i == lo[0] || i == hi[0] ||
                                        j == lo[1] || j == hi[1] ||
                                        k == lo[2] || k == hi[2];
                                    mismatch_covered += (cv != 0);
                                    mismatch_interface += (iv != 0);
                                    mismatch_physical += physical;
                                    mismatch_fab_edge += fab_edge;
                                    if (d > max_diff) {
                                        max_diff = d;
                                        max_iv = IntVect(AMREX_D_DECL(i, j, k));
                                        max_covered = cv != 0;
                                        max_interface = iv != 0;
                                        max_physical = physical;
                                        max_fab_edge = fab_edge;
                                    }
                                }
                            }
                        }
                    }
                    amrex::Print()
                        << "ddf_mismatch_location: lev=" << lev
                        << " q=" << q
                        << " count_gt_1e-12=" << mismatch_count
                        << " covered=" << mismatch_covered
                        << " interface=" << mismatch_interface
                        << " physical_boundary=" << mismatch_physical
                        << " fab_edge=" << mismatch_fab_edge
                        << " max=" << max_diff
                        << " max_iv=" << max_iv
                        << " max_is_covered=" << max_covered
                        << " max_is_interface=" << max_interface
                        << " max_is_physical_boundary=" << max_physical
                        << " max_is_fab_edge=" << max_fab_edge
                        << '\n';

                    if (has_fine) {
                        for (MFIter mfi(difference_batch, false);
                             mfi.isValid(); ++mfi) {
                            const Box bx = mfi.validbox();
                            const auto diff =
                                difference_batch.array(mfi);
                            const auto covered =
                                covered_mask.at(lev).const_array(mfi);
                            const auto interface =
                                interface_mask.at(lev).const_array(mfi);
                            amrex::ParallelFor(
                                bx,
                                [=] AMREX_GPU_DEVICE(
                                    int i, int j, int k) noexcept {
                                    const bool active = covered(i, j, k) == 0;
                                    if (!active) {
                                        diff(i, j, k, n) = Real(0.0);
                                    }
                                });
                        }
                    }

                    const Real active_linf =
                        difference_batch.norminf(n);
                    const Real active_l1 =
                        difference_batch.norm1(n);
                    const Real active_l2 =
                        difference_batch.norm2(n);

                    for (MFIter mfi(difference_batch, false);
                         mfi.isValid(); ++mfi) {
                        const Box bx = mfi.validbox();
                        const auto values =
                            difference_batch.array(mfi);
                        const auto ref =
                            reference_on_current.const_array(mfi);
                        if (has_fine) {
                            const auto covered =
                                covered_mask.at(lev).const_array(mfi);
                            const auto interface =
                                interface_mask.at(lev).const_array(mfi);
                            amrex::ParallelFor(
                                bx,
                                [=] AMREX_GPU_DEVICE(
                                    int i, int j, int k) noexcept {
                                    const bool active = covered(i, j, k) == 0;
                                    values(i, j, k, n) =
                                        active ? ref(i, j, k, q)
                                               : Real(0.0);
                                });
                        } else {
                            amrex::ParallelFor(
                                bx,
                                [=] AMREX_GPU_DEVICE(
                                    int i, int j, int k) noexcept {
                                    values(i, j, k, n) =
                                        ref(i, j, k, q);
                                });
                        }
                    }
                    const Real active_ref_l2 =
                        difference_batch.norm2(n);
                    const Real active_rel_l2 =
                        active_ref_l2 > 0.0
                            ? active_l2 / active_ref_l2
                            : 0.0;
                    const Real valid_l1_mean =
                        valid_cells > 0
                            ? valid_l1 / Real(valid_cells)
                            : 0.0;
                    const Real active_l1_mean =
                        active_cells > 0
                            ? active_l1 / Real(active_cells)
                            : 0.0;
                    level_valid_linf =
                        std::max(level_valid_linf, valid_linf);
                    level_valid_l1 += valid_l1;
                    level_valid_l2_sq += valid_l2 * valid_l2;
                    level_active_linf =
                        std::max(level_active_linf, active_linf);
                    level_active_l1 += active_l1;
                    level_active_l2_sq += active_l2 * active_l2;
                    level_active_ref_l2_sq +=
                        active_ref_l2 * active_ref_l2;

                    amrex::Print()
                        << "ddf_cell_norm_component: lev=" << lev
                        << " q=" << q
                        << " valid_linf=" << valid_linf
                        << " valid_l1=" << valid_l1
                        << " valid_l1_mean=" << valid_l1_mean
                        << " valid_l2=" << valid_l2
                        << " active_linf=" << active_linf
                        << " active_l1=" << active_l1
                        << " active_l1_mean=" << active_l1_mean
                        << " active_l2=" << active_l2
                        << " active_ref_l2=" << active_ref_l2
                        << " active_rel_l2=" << active_rel_l2
                        << '\n';
                }
            }

            const Real level_valid_l2 =
                std::sqrt(level_valid_l2_sq);
            const Real level_active_l2 =
                std::sqrt(level_active_l2_sq);
            const Real level_active_ref_l2 =
                std::sqrt(level_active_ref_l2_sq);
            const Real level_active_rel_l2 =
                level_active_ref_l2 > 0.0
                    ? level_active_l2 / level_active_ref_l2
                    : 0.0;
            const Real level_valid_l1_mean =
                valid_cells > 0
                    ? level_valid_l1 / Real(valid_cells * Q)
                    : 0.0;
            const Real level_active_l1_mean =
                active_cells > 0
                    ? level_active_l1 / Real(active_cells * Q)
                    : 0.0;
            global_linf = std::max(global_linf, level_active_linf);
            global_l1 += level_active_l1;
            global_diff_l2_sq +=
                level_active_l2 * level_active_l2;
            global_ref_l2_sq +=
                level_active_ref_l2 * level_active_ref_l2;
            amrex::Print()
                << "ddf_cell_norm_level: lev=" << lev
                << " valid_cells=" << state.boxArray().numPts()
                << " valid_linf=" << level_valid_linf
                << " valid_l1=" << level_valid_l1
                << " valid_l1_mean=" << level_valid_l1_mean
                << " valid_l2=" << level_valid_l2
                << " active_linf=" << level_active_linf
                << " active_l1=" << level_active_l1
                << " active_l1_mean=" << level_active_l1_mean
                << " active_l2=" << level_active_l2
                << " active_ref_l2=" << level_active_ref_l2
                << " active_rel_l2=" << level_active_rel_l2
                << '\n';
            continue;
        }

        const MultiFab& current = f_old[lev];

        MultiFab difference(
            current.boxArray(), current.DistributionMap(), Q, 0);
        MultiFab::Copy(difference, current, 0, 0, Q, 0);
        MultiFab::Subtract(
            difference, reference_on_current, 0, 0, Q, 0);

        const Real level_linf =
            difference.norm0(0, Q, IntVect(0));
        const Real level_diff_l2 = difference.norm2(0, Q);
        Real level_diff_l1 = 0.0;
        for (int q = 0; q < Q; ++q) {
            level_diff_l1 += difference.norm1(q);
        }
        const Real level_ref_l2 = reference_on_current.norm2(0, Q);
        const Real level_rel_l2 =
            level_ref_l2 > 0.0 ? level_diff_l2 / level_ref_l2 : 0.0;

        global_linf = std::max(global_linf, level_linf);
        global_l1 += level_diff_l1;
        global_diff_l2_sq += level_diff_l2 * level_diff_l2;
        global_ref_l2_sq += level_ref_l2 * level_ref_l2;

        amrex::Print() << "ddf_norm_level: lev=" << lev
                       << " valid_cells=" << current.boxArray().numPts()
                       << " linf=" << level_linf
                       << " l1=" << level_diff_l1
                       << " l2=" << level_diff_l2
                       << " ref_l2=" << level_ref_l2
                       << " rel_l2=" << level_rel_l2 << '\n';

        for (int q = 0; q < Q; ++q) {
            const Real linf = difference.norminf(q);
            const Real diff_l2 = difference.norm2(q);
            const Real ref_l2 = reference_on_current.norm2(q);
            const Real rel_l2 =
                ref_l2 > 0.0 ? diff_l2 / ref_l2 : 0.0;
            Long mismatch_count = 0;
            Long mismatch_covered = 0;
            Long mismatch_interface = 0;
            Long mismatch_physical = 0;
            Long mismatch_fab_edge = 0;
            Real max_diff = 0.0;
            IntVect max_iv(0);
            const Box domain = Geom(lev).Domain();
            const bool has_fine = lev < finest_level && cf_mask_mode == 1;
            for (MFIter mfi(difference, false); mfi.isValid(); ++mfi) {
                const Box bx = mfi.validbox();
                FArrayBox host_diff(mfi.validbox(), difference.nComp(), The_Pinned_Arena());
                Gpu::dtoh_memcpy(host_diff.dataPtr(), difference[mfi].dataPtr(), host_diff.nBytes());
                const auto diff = host_diff.const_array();
                const auto covered = has_fine ? covered_mask.at(lev).const_array(mfi) : Array4<const int>{};
                const auto interface = has_fine ? interface_mask.at(lev).const_array(mfi) : Array4<const int>{};
                const IntVect lo = bx.smallEnd();
                const IntVect hi = bx.bigEnd();
                for (int k = lo[2]; k <= hi[2]; ++k)
                    for (int j = lo[1]; j <= hi[1]; ++j)
                        for (int i = lo[0]; i <= hi[0]; ++i) {
                            const Real d = std::abs(diff(i, j, k, q));
                            if (d <= Real(1.e-12))
                                continue;
                            ++mismatch_count;
                            const bool physical =
                                (!Geom(lev).isPeriodic(0) && (i == domain.smallEnd(0) || i == domain.bigEnd(0))) ||
                                (!Geom(lev).isPeriodic(1) && (j == domain.smallEnd(1) || j == domain.bigEnd(1))) ||
                                (!Geom(lev).isPeriodic(2) && (k == domain.smallEnd(2) || k == domain.bigEnd(2)));
                            const bool fab_edge = i == lo[0] || i == hi[0] || j == lo[1] || j == hi[1] || k == lo[2] || k == hi[2];
                            mismatch_covered += has_fine && covered(i, j, k) != 0;
                            mismatch_interface += has_fine && interface(i, j, k) != 0;
                            mismatch_physical += physical;
                            mismatch_fab_edge += fab_edge;
                            if (d > max_diff) {
                                max_diff = d;
                                max_iv = IntVect(AMREX_D_DECL(i, j, k));
                            }
                        }
            }
            amrex::Print() << "ddf_mismatch_location: lev=" << lev << " q=" << q
                           << " count_gt_1e-12=" << mismatch_count
                           << " covered=" << mismatch_covered
                           << " interface=" << mismatch_interface
                           << " physical_boundary=" << mismatch_physical
                           << " fab_edge=" << mismatch_fab_edge
                           << " max=" << max_diff << " max_iv=" << max_iv << '\n';
            amrex::Print() << "ddf_norm_component: lev=" << lev
                           << " q=" << q
                           << " linf=" << linf
                           << " l2=" << diff_l2
                           << " ref_l2=" << ref_l2
                           << " rel_l2=" << rel_l2 << '\n';
        }
    }

    const Real global_diff_l2 = std::sqrt(global_diff_l2_sq);
    const Real global_ref_l2 = std::sqrt(global_ref_l2_sq);
    const Real global_rel_l2 =
        global_ref_l2 > 0.0 ? global_diff_l2 / global_ref_l2 : 0.0;
    amrex::Print() << "ddf_norm_global: linf=" << global_linf
                   << " l1=" << global_l1
                   << " l2=" << global_diff_l2
                   << " ref_l2=" << global_ref_l2
                   << " rel_l2=" << global_rel_l2 << '\n'
                   << "ddf_norm_end\n";
}

void AmrCoreLBM::ReadCheckpoint() {
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(params_.begin_step > 0,
                                     "checkpoint.begin_step must be > 0 when restarting.");
    const std::string chkname = amrex::Concatenate(params_.chk_prefix, params_.begin_step, 8);
    const std::string header_file = chkname + "/Header";

    if (!amrex::FileExists(header_file)) {
        amrex::Abort("[Checkpoint] Specified restart directory '" + chkname + "' is missing Header file.");
    }

    if (ParallelDescriptor::IOProcessor()) {
        amrex::Print() << "[Checkpoint] Restart directory resolved to '" << chkname
                       << "' (begin_step=" << params_.begin_step << ")" << std::endl;
    }

    amrex::Vector<char> header_chars;
    ParallelDescriptor::ReadAndBcastFile(header_file, header_chars);
    std::string header_str(header_chars.dataPtr());
    std::istringstream is(header_str); // 随后将字符串包装成输入流，便于逐行解析。

    std::string label;
    std::getline(is, label);
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        label == checkpoint_label_v1 || label == checkpoint_label_v2,
        "Invalid checkpoint header");

    // 版本1采用旧式A‑B格式。它的DDF数组本身已经是标准格式，
    // 因此可以直接作为标准输入加载。版本2对数据布局进行命名，
    // 并且不会将未识别的数组视作OSI原始数据。

    std::string stored_layout = checkpoint_layout_ab;
    if (label == checkpoint_label_v2) {
        std::getline(is, stored_layout);
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            stored_layout == checkpoint_layout_ab ||
                stored_layout == checkpoint_layout_osi,
            "Unsupported checkpoint DDF layout: " + stored_layout);
    }

    {
        std::string tmp;
        std::getline(is, tmp);
    }
    {
        std::string tmp;
        std::getline(is, tmp);
    }

    int finest_in_file;
    is >> finest_in_file; // getline适合读字符串, operator>> 适合读取整数、浮点数等结构化数据
    {
        std::string tmp;
        std::getline(is, tmp);
    }

    amrex::Vector<amrex::BoxArray> ba_file(finest_in_file + 1);
    amrex::Vector<amrex::DistributionMapping> dm_file(finest_in_file + 1);
    for (int lev = 0; lev <= finest_in_file; ++lev) {
        ba_file[lev].readFrom(is);
        {
            std::string tmp;
            std::getline(is, tmp);
        }
        dm_file[lev].readFrom(is);
        {
            std::string tmp;
            std::getline(is, tmp);
        }
    }

    SetFinestLevel(finest_in_file); // 将当前对象的最高层级设置为 checkpoint 中保存的层级。
    for (int lev = 0; lev <= finest_level; ++lev) {
        SetBoxArray(lev, ba_file[lev]);
        /* checkpoint 文件
            ↓ VisMF::Read
        按照文件中原有布局读取
            ↓ ParallelCopy
        按照当前 MPI 进程数重新分配 */
        SetDistributionMap(lev, DistributionMapping(ba_file[lev]));
    }

    const std::string level_prefix = "Level_";
    for (int lev = 0; lev <= finest_level; ++lev) {
        ClearLevel(lev); // 确保接下来的 define() 是从干净状态开始
        velocity[lev].define(boxArray(lev), DistributionMap(lev), AMREX_SPACEDIM, nghost);
        density[lev].define(boxArray(lev), DistributionMap(lev), 1, nghost);
        vorticity[lev].define(boxArray(lev), DistributionMap(lev), 2, nghost);
        shear[lev].define(boxArray(lev), DistributionMap(lev), 1, nghost);
        force[lev].define(boxArray(lev), DistributionMap(lev), AMREX_SPACEDIM, nghost);

        const std::string old_name = MultiFabFileFullPrefix(
            lev, chkname, level_prefix, "f_old");
        MultiFab stored_old;
        VisMF::Read(stored_old, old_name); // 读取 f_old
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            stored_old.nComp() == Q &&
                stored_old.boxArray() == boxArray(lev),
            "Checkpoint canonical DDF does not match the level BoxArray");

        MultiFab canonical(boxArray(lev), DistributionMap(lev), Q, 0);
        canonical.ParallelCopy(
            stored_old, 0, 0, Q, IntVect(0), IntVect(0),
            Geom(lev).periodicity());

        if (stream_mode == 1) {
            InitializeOsiLevel(
                lev, boxArray(lev), DistributionMap(lev));
            osi_state[lev].setVal(
                std::numeric_limits<Real>::quiet_NaN());
            MultiFab::Copy(osi_state[lev], canonical, 0, 0, Q, 0);
            osi_phase[lev] = 0;

            if (osi_ab_check) {
                f_old[lev].define(
                    boxArray(lev), DistributionMap(lev), Q, nghost);
                f_new[lev].define(
                    boxArray(lev), DistributionMap(lev), Q, nghost);
                MultiFab::Copy(f_old[lev], canonical, 0, 0, Q, 0);
            }
        } else {
            f_old[lev].define(
                boxArray(lev), DistributionMap(lev), Q, nghost);
            f_new[lev].define(
                boxArray(lev), DistributionMap(lev), Q, nghost);
            MultiFab::Copy(f_old[lev], canonical, 0, 0, Q, 0);
        }

        velocity[lev].setVal(0.0, nghost);
        density[lev].setVal(0.0, nghost);
        vorticity[lev].setVal(0.0, nghost);
        shear[lev].setVal(0.0, nghost);
        force[lev].setVal(0.0, nghost);
    }

    if (params_.write_particles) {
        for (int i = 0; i < particle_num; ++i) {
            const std::string pname = "particles_" + std::to_string(i);
            particles[i].reset();
            particles[i] = std::make_unique<LagrangeParticleContainer>(this, points[i], i, grid_);
            particles[i]->Restart(chkname, pname);
        }
    }

    RebuildCoarseFineCaches();
}
