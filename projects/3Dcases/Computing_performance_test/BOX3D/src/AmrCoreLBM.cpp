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
constexpr int cf_interface_mask_nghost = 2;
constexpr int cf_covered_mask_nghost = cf_interface_mask_nghost + 1;

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
AmrCoreLBM::AmrCoreLBM(amrex::Geometry const& level_0_geom, amrex::AmrInfo const& amr_info)
    : AmrCore(level_0_geom, amr_info) {
    ReadParameters();

    int nlevs_max = max_level + 1;

    f_new.resize(nlevs_max);
    f_old.resize(nlevs_max);
    average_down_buffer.resize(nlevs_max);
    average_interface_buffer.resize(nlevs_max);
    average_interface_fine_box.resize(nlevs_max);
    covered_mask.resize(nlevs_max);
    interface_mask.resize(nlevs_max);
    covered_cell_counts.resize(nlevs_max, 0);
    interface_cell_counts.resize(nlevs_max, 0);
    interp_scale_work_boxes.resize(nlevs_max);
    interp_direct_coarse_stage.resize(nlevs_max);
    interp_direct_fine_boxes.resize(nlevs_max);
    interp_direct_fine_index.resize(nlevs_max);
    interp_direct_needs_physical_fill.resize(nlevs_max);
    interp_direct_cache_ready.resize(nlevs_max, 0);
    boundary_work_boxes.resize(nlevs_max);

    velocity.resize(nlevs_max);
    vorticity.resize(nlevs_max);
    shear.resize(nlevs_max);
    density.resize(nlevs_max);
    force.resize(nlevs_max);

    tau.resize(nlevs_max);
    tau[0] = tau_0;

    for (int lev = 1; lev <= max_level; ++lev) {
        tau[lev] = 2 * (tau[lev - 1] - 0.5) + 0.5;
    }

    int bc_lo[] = {BCType::foextrap, BCType::foextrap, BCType::foextrap};
    int bc_hi[] = {BCType::foextrap, BCType::foextrap, BCType::foextrap};

    bcs.resize(Q);

    for (int idim = 0; idim < AMREX_SPACEDIM; idim++) {
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
    points[0] = {X, Y, Z};
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
    amrex::Print() << std::setw(15) << std::left << "  dx_0   =" << std::setw(10) << std::right << dx_0 << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  dx_min =" << std::setw(10) << std::right << dx_min << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  Re     =" << std::setw(10) << std::right << Re << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  cs2    =" << std::setw(10) << std::right << cs2 << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  p0     =" << std::setw(10) << std::right << p0 << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  Ma     =" << std::setw(10) << std::right << Ma << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  U0     =" << std::setw(10) << std::right << U0 << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  cf_mask=" << std::setw(10) << std::right << cf_mask_mode << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  col_mode=" << std::setw(10) << std::right << collide_mode << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  avg_mode=" << std::setw(10) << std::right << average_mode << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  cf_interp=" << std::setw(10) << std::right << cf_interp_mode << std::endl;

    for (int lev = 0; lev <= finest_level; lev++) {
        amrex::Print() << std::setw(15) << std::left << "  tau    =" << std::setw(10) << std::right << tau[lev] << std::endl;
    }

    amrex::Print() << "╚════════════════════════════════════════════════════════╝" << std::endl;
    amrex::Print() << std::endl;
}
void AmrCoreLBM::ReadParameters() {
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
        pp.query("average_mode", average_mode);
        if (average_mode < 0 || average_mode > 3) {
            amrex::Abort("lbm.average_mode must be 0, 1, 2, or 3");
        }
        pp.query("cf_interp_mode", cf_interp_mode);
        if (cf_interp_mode < 0 || cf_interp_mode > 3) {
            amrex::Abort("lbm.cf_interp_mode must be 0, 1, 2, or 3");
        }
        int n = pp.countval("err");
        if (n > 0) {
            pp.getarr("err", err, 0, n);
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
    // amrex::AllPrint()<<"FillCoarse Patch from " << lev-1 << " to " << lev <<std::endl;

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
    // amrex::AllPrint()<<"FillPatch from " << lev-1 << " to " << lev <<std::endl;

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

void AmrCoreLBM::BuildDirectInterpolationCache(int lev) {
    const double start = amrex::second();
    // 启动阶段的递归循环可能在 finest_level 提升前请求已分配的下一层；
    // 正常已安装层由 RebuildCoarseFineCaches() 预构建，其余情况延迟构建。
    AMREX_ALWAYS_ASSERT(lev > 0 && lev <= max_level);

    const auto fill_ng = f_old[lev].nGrowVect(); // 获取f_old[lev] 在 x、y、z 三个方向上分配的 ghost cell 层数。
    const auto& coarsener =
        cell_bilinear_interp.BoxCoarsener(refRatio(lev - 1)); // 根据细化比和 cell_bilinear_interp 插值模板，把 fine 目标区域转换成所需 coarse stencil 区域的 Box 转换器。
    const BoxArray& fine_ba = f_old[lev].boxArray();
    const BoxArray fine_ba_simplified = fine_ba.simplified();
    Box fine_domain = Geom(lev).Domain();
    for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
        if (Geom(lev).isPeriodic(dir)) {
            fine_domain.grow(dir, fill_ng[dir]);
        }
    }

    Vector<Box> coarse_boxes;
    Vector<int> coarse_owners;
    auto& coarse_stage = interp_direct_coarse_stage[lev];
    auto& fine_work_boxes = interp_direct_fine_boxes[lev];
    auto& fine_indices = interp_direct_fine_index[lev];
    auto& needs_physical_fill = interp_direct_needs_physical_fill[lev];
    coarse_stage.clear();
    fine_work_boxes.clear();
    fine_indices.clear();
    needs_physical_fill.clear();

    for (int fine_index = 0; fine_index < fine_ba.size(); ++fine_index) {
        const Box target =
            amrex::grow(fine_ba[fine_index], fill_ng) & fine_domain;
        const BoxList leftover = fine_ba_simplified.complementIn(target);
        for (const Box& work_box : leftover) {
            const Box coarse_box = coarsener.doit(work_box);
            const int owner = f_old[lev].DistributionMap()[fine_index];
            bool needs_fill = false;
            for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
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
            const int stage_index = static_cast<int>(coarse_boxes.size());
            coarse_boxes.push_back(coarse_box);
            coarse_owners.push_back(owner);
            fine_work_boxes.push_back({});
            fine_indices.push_back({});
            needs_physical_fill.push_back(
                static_cast<unsigned char>(needs_fill));
            fine_work_boxes[stage_index].push_back(work_box);
            fine_indices[stage_index].push_back(fine_index);
        }
    }

    if (!coarse_boxes.empty()) {
        BoxArray coarse_stage_ba(
            coarse_boxes.data(), static_cast<int>(coarse_boxes.size()));
        DistributionMapping coarse_stage_dm(std::move(coarse_owners));
        coarse_stage.define(coarse_stage_ba, coarse_stage_dm, Q, 0);
    }
    interp_direct_cache_ready[lev] = 1;
    ++perf_stats.interp_cache_builds;
    perf_stats.interp_cache_build += amrex::second() - start;
}

void AmrCoreLBM::FillDdfPatch(int lev, amrex::Real time, amrex::MultiFab& mf) // 加入缩放
{
    // amrex::AllPrint()<<"FillDdfPatch from " << lev-1 << " to " << lev <<std::endl;

    // 原 AMReX 守恒线性插值，保留以便对照。
    // Interpolater* mapper = &cell_cons_interp;

    // Jaber 论文式三线性插值：8 个粗格点加权。
    Interpolater* mapper = &cell_bilinear_interp;

    amrex::MultiFab& f_new_lev_c = f_new[lev - 1]; // 用f_new当缓存容器
    amrex::MultiFab& f_old_lev_f = f_old[lev];     // 保持和传入的mf一致

    amrex::Vector<amrex::MultiFab*> cmf{&f_new_lev_c};
    amrex::Vector<amrex::MultiFab*> fmf{&f_old_lev_f};

    amrex::Vector<Real> ctime{time};
    amrex::Vector<Real> ftime{time};

    // 如果是粗网格插值到细网格valid,无论如何只需要操作粗网格就可以了。
    amrex::MultiFab& f_old_lev_c = f_old[lev - 1];
    amrex::Real scale = tau[lev] / tau[lev - 1] / 2.0;

    const amrex::IntVect fill_ng = mf.nGrowVect();
    const auto& coarsener = mapper->BoxCoarsener(refRatio(lev - 1));

    const bool direct_target =
        &mf == &f_old_lev_f &&
        mf.getBDKey() == f_old_lev_f.getBDKey();

    if (cf_interp_mode >= 2 && direct_target) {
        ScopedPerfTimer timer(perf_stats.interp_fillpatch);
        auto& coarse_stage = interp_direct_coarse_stage[lev];
        auto& fine_work_boxes = interp_direct_fine_boxes[lev];
        auto& fine_indices = interp_direct_fine_index[lev];

        if (!interp_direct_cache_ready[lev]) {
            BuildDirectInterpolationCache(lev);
        }

        if (!fine_indices.empty()) {
            coarse_stage.ParallelCopy(
                f_old_lev_c, 0, 0, Q, amrex::IntVect(0),
                amrex::IntVect(0), Geom(lev - 1).periodicity());

            // AmrCoreFill 不施加额外 ext_dir 数值；通用 PhysBCFunct 在这里
            // 等价于把非周期域外 stencil 复制为最近的域内 coarse 值。
            // 只为真正越过物理边界的 staging Box 启动复制 kernel，避免
            // 每次插值对全部离散 Fab 执行通用边界管理。
            const Box coarse_domain = Geom(lev - 1).Domain();
            const auto coarse_lo = amrex::lbound(coarse_domain);
            const auto coarse_hi = amrex::ubound(coarse_domain);
            const auto coarse_periodic =
                Geom(lev - 1).isPeriodicArray();
            for (MFIter mfi(coarse_stage, false); mfi.isValid(); ++mfi) {
                const int stage_index = mfi.index();
                if (!interp_direct_needs_physical_fill[lev][stage_index]) {
                    continue;
                }

                const Box bx = mfi.validbox();
                const auto coarse = coarse_stage.array(mfi);
                amrex::ParallelFor(
                    bx, Q,
                    [=] AMREX_GPU_DEVICE(int i, int j, int k, int q) {
                        const int src_i =
                            coarse_periodic[0]
                                ? i
                                : (i < coarse_lo.x
                                       ? coarse_lo.x
                                       : (i > coarse_hi.x ? coarse_hi.x : i));
                        const int src_j =
                            coarse_periodic[1]
                                ? j
                                : (j < coarse_lo.y
                                       ? coarse_lo.y
                                       : (j > coarse_hi.y ? coarse_hi.y : j));
                        const int src_k =
                            coarse_periodic[2]
                                ? k
                                : (k < coarse_lo.z
                                       ? coarse_lo.z
                                       : (k > coarse_hi.z ? coarse_hi.z : k));
                        if (src_i != i || src_j != j || src_k != k) {
                            coarse(i, j, k, q) =
                                coarse(src_i, src_j, src_k, q);
                        }
                    });
            }

            if (cf_interp_mode != 3) {
                for (MFIter mfi(coarse_stage, false); mfi.isValid(); ++mfi) {
                    const auto coarse = coarse_stage.array(mfi);
                    const Box bx = mfi.validbox();
                    amrex::ParallelFor(
                        bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                            average_scale(i, j, k, coarse, scale);
                        });
                }
            }

            const auto ratio = refRatio(lev - 1);
            const bool fused_interp = cf_interp_mode == 3;
            for (MFIter mfi(coarse_stage, false); mfi.isValid(); ++mfi) {
                const int stage_index = mfi.index();
                const auto coarse = coarse_stage.const_array(mfi);
                for (int work_index = 0;
                     work_index < static_cast<int>(
                                      fine_work_boxes[stage_index].size());
                     ++work_index) {
                    const int fine_index =
                        fine_indices[stage_index][work_index];
                    const auto fine = mf.array(fine_index);
                    const Box& bx = fine_work_boxes[stage_index][work_index];
                    amrex::ParallelFor(
                        bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                            if (fused_interp) {
                                interp_bilinear_d3q_scaled(
                                    i, j, k, fine, coarse, ratio, scale);
                            } else {
                                interp_bilinear_d3q(
                                    i, j, k, fine, coarse, ratio);
                            }
                        });
                }
            }
        }

        // FillPatchTwoLevels 的最终步骤：同层 fine valid 数据覆盖 coarse
        // 插值结果，随后再应用细层物理边界条件。
        mf.FillBoundary(0, Q, fill_ng, Geom(lev).periodicity());
        if (Gpu::inLaunchRegion()) {
            GpuBndryFuncFab<AmrCoreFill> gpu_bndry_func(AmrCoreFill{});
            PhysBCFunct<GpuBndryFuncFab<AmrCoreFill>> fphysbc(
                geom[lev], bcs, gpu_bndry_func);
            fphysbc(mf, 0, Q, fill_ng, time, 0);
        } else {
            CpuBndryFuncFab bndry_func(nullptr);
            PhysBCFunct<CpuBndryFuncFab> fphysbc(
                geom[lev], bcs, bndry_func);
            fphysbc(mf, 0, Q, fill_ng, time, 0);
        }
        return;
    }

    // 通用路径沿用 TheFPinfo 的目标区域。mode 2 的正常时间推进已在
    // 上方返回，因此不会再逐次遍历 patch 元数据；布局变化时仍可安全回退。
    const auto& fpc = FabArrayBase::TheFPinfo(
        f_old_lev_f, mf, fill_ng, coarsener,
        Geom(lev), Geom(lev - 1), nullptr);
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

    if (cf_interp_mode >= 1) {
        ScopedPerfTimer timer(perf_stats.interp_fillpatch);

        if (!fpc.ba_crse_patch.empty()) {
            amrex::MultiFab coarse_patch(
                fpc.ba_crse_patch, fpc.dm_patch, Q, 0, amrex::MFInfo(),
                *fpc.fact_crse_patch);
            amrex::MultiFab fine_patch(
                fpc.ba_fine_patch, fpc.dm_patch, Q, 0, amrex::MFInfo(),
                *fpc.fact_fine_patch);

            coarse_patch.setDomainBndry(
                std::numeric_limits<Real>::quiet_NaN(), Geom(lev - 1));
            coarse_patch.ParallelCopy(
                f_old_lev_c, 0, 0, Q, amrex::IntVect(0),
                amrex::IntVect(0), Geom(lev - 1).periodicity());
            if (Gpu::inLaunchRegion()) {
                GpuBndryFuncFab<AmrCoreFill> gpu_bndry_func(AmrCoreFill{});
                PhysBCFunct<GpuBndryFuncFab<AmrCoreFill>> cphysbc(
                    geom[lev - 1], bcs, gpu_bndry_func);
                cphysbc(coarse_patch, 0, Q, coarse_patch.nGrowVect(),
                        time, 0);
            } else {
                CpuBndryFuncFab bndry_func(nullptr);
                PhysBCFunct<CpuBndryFuncFab> cphysbc(
                    geom[lev - 1], bcs, bndry_func);
                cphysbc(coarse_patch, 0, Q, coarse_patch.nGrowVect(),
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

            const auto ratio = refRatio(lev - 1);
            for (MFIter mfi(fine_patch, false); mfi.isValid(); ++mfi) {
                const auto fine = fine_patch.array(mfi);
                const auto coarse = coarse_patch.const_array(mfi);
                const Box bx = mfi.validbox();
                amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                    interp_bilinear_d3q(i, j, k, fine, coarse, ratio);
                });
            }

            mf.ParallelCopy(fine_patch, 0, 0, Q, amrex::IntVect(0),
                            fill_ng, Geom(lev).periodicity());
        }

        // 与 AMReX 保持相同优先级：coarse 插值先写，最后由同层 fine
        // valid 数据覆盖重叠 ghost。RemakeLevel 的目标布局不同时使用
        // ParallelCopy，正常时间推进则直接 FillBoundary。
        if (&mf == &f_old_lev_f) {
            mf.FillBoundary(0, Q, fill_ng,
                            Geom(lev).periodicity());
        } else {
            mf.ParallelCopy(f_old_lev_f, 0, 0, Q, amrex::IntVect(0),
                            fill_ng, Geom(lev).periodicity());
        }

        if (Gpu::inLaunchRegion()) {
            GpuBndryFuncFab<AmrCoreFill> gpu_bndry_func(AmrCoreFill{});
            PhysBCFunct<GpuBndryFuncFab<AmrCoreFill>> fphysbc(
                geom[lev], bcs, gpu_bndry_func);
            fphysbc(mf, 0, Q, fill_ng, time, 0);
        } else {
            CpuBndryFuncFab bndry_func(nullptr);
            PhysBCFunct<CpuBndryFuncFab> fphysbc(
                geom[lev], bcs, bndry_func);
            fphysbc(mf, 0, Q, fill_ng, time, 0);
        }
        return;
    }

    {
        ScopedPerfTimer timer(perf_stats.interp_scale);
        // 用于判断当前传入 FillDdfPatch() 的目标细层 mf，是否与此前构造 interp_scale_work_boxes 缓存时使用的细层网格布局一致。
        const bool cache_matches_target =
            lev <= finest_level && mf.getBDKey() == f_old_lev_f.getBDKey();
        for (MFIter mfi(f_old_lev_c, false); mfi.isValid(); ++mfi) {
            const Array4<Real>& fold = f_old_lev_c.array(mfi);
            const Array4<Real>& fnew = f_new_lev_c.array(mfi);
            perf_stats.interp_scale_full_cells += mfi.validbox().numPts();

            if (cache_matches_target) {
                for (const Box& bx : interp_scale_work_boxes[lev - 1][mfi.index()]) {
                    perf_stats.interp_scale_cells += bx.numPts();
                    ++perf_stats.interp_scale_launch_boxes;
                    amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                        interp_scale(i, j, k, fold, fnew, scale);
                    });
                }
            } else {
                const Box bx = mfi.validbox();
                perf_stats.interp_scale_cells += bx.numPts();
                ++perf_stats.interp_scale_launch_boxes;
                amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                    interp_scale(i, j, k, fold, fnew, scale);
                });
            }
        }
    }

    {
        ScopedPerfTimer timer(perf_stats.interp_fillpatch);
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
                                      refRatio(lev - 1), mapper, bcs, 0); // 如果time与ftime匹配,则会把细网格覆盖过去。
        }
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

void AmrCoreLBM::RefineMesh(amrex::Real cur_time) {
    regrid_tag_counts.assign(max_level + 1, -1);
    for (auto& buffer : average_down_buffer) {
        buffer.clear();
    }
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
    std::fill(
        interp_direct_cache_ready.begin(),
        interp_direct_cache_ready.end(), 0);
    regrid(0, cur_time);
    RebuildCoarseFineCaches();

    if (ParallelDescriptor::IOProcessor()) {
        amrex::Print() << "regrid_observe: finest_level=" << finest_level << '\n';
        for (int lev = 0; lev <= finest_level; ++lev) {
            const auto& ba = boxArray(lev);
            amrex::Print() << "regrid_observe: lev=" << lev
                           << " boxes=" << ba.size() // 返回该 BoxArray 中的 Box 总数
                           << " valid_cells=" << ba.numPts();
            if (lev < max_level && regrid_tag_counts[lev] >= 0) {
                amrex::Print() << " tagged_to_lev=" << (lev + 1)
                               << " tag_cells=" << regrid_tag_counts[lev];
            }
            amrex::Print() << '\n';
        }
        for (int lev = 0; lev < finest_level; ++lev) {
            if (cf_mask_mode == 0) {
                amrex::Print() << "cf_mask_observe: lev=" << lev << " disabled\n";
                continue;
            }

            long fine_cells_per_coarse = 1;
            const auto ratio = refRatio(lev);
            for (int dim = 0; dim < AMREX_SPACEDIM; ++dim) {
                fine_cells_per_coarse *= ratio[dim];
            }

            const auto expected_covered = boxArray(lev + 1).numPts() / fine_cells_per_coarse;
            const auto uncovered = boxArray(lev).numPts() - covered_cell_counts[lev];
            amrex::Print() << "cf_mask_observe: lev=" << lev
                           << " covered=" << covered_cell_counts[lev]
                           << " covered_expected=" << expected_covered
                           << " interface=" << interface_cell_counts[lev]
                           << " uncovered=" << uncovered << '\n';
        }
    }
}

void AmrCoreLBM::RebuildCoarseFineCaches() {
    covered_cell_counts.assign(max_level + 1, 0);
    interface_cell_counts.assign(max_level + 1, 0);

    for (int lev = 0; lev <= max_level; ++lev) {
        covered_mask[lev].clear();
        interface_mask[lev].clear();
        interp_scale_work_boxes[lev].clear();
        interp_direct_coarse_stage[lev].clear();
        interp_direct_fine_boxes[lev].clear();
        interp_direct_fine_index[lev].clear();
        interp_direct_cache_ready[lev] = 0;
        boundary_work_boxes[lev].clear();
        average_down_buffer[lev].clear();
        average_interface_buffer[lev].clear();
        average_interface_fine_box[lev].clear();
    }

    RebuildCoarseFineMasks();
    BuildBoundaryWorkBoxes();
    BuildInterpolationCache();
    BuildRestrictionCache();
}

void AmrCoreLBM::RebuildCoarseFineMasks() {
    if (cf_mask_mode == 0) {
        return;
    }

    for (int lev = 0; lev < finest_level; ++lev) {
        const Box domain = Geom(lev).Domain();
        covered_mask[lev] = amrex::makeFineMask(
            f_old[lev], f_old[lev + 1], amrex::IntVect(cf_covered_mask_nghost), refRatio(lev),
            Geom(lev).periodicity(), 0, 1);

        interface_mask[lev].define(f_old[lev].boxArray(), f_old[lev].DistributionMap(), 1,
                                   cf_interface_mask_nghost);
        interface_mask[lev].setVal(0);
        const auto domain_lo = amrex::lbound(domain);
        const auto domain_hi = amrex::ubound(domain);

        for (MFIter mfi(interface_mask[lev], TilingIfNotGPU()); mfi.isValid(); ++mfi) {
            const Box bx = mfi.growntilebox(cf_interface_mask_nghost) & domain;
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
                            const bool in_domain =
                                (ni >= domain_lo.x && ni <= domain_hi.x) &&
                                (nj >= domain_lo.y && nj <= domain_hi.y) &&
                                (nk >= domain_lo.z && nk <= domain_hi.z);
                            if (in_domain && covered(ni, nj, nk) == 0) {
                                interface(i, j, k) = 1;
                                return;
                            }
                        }
                    }
                }
            });
        }

        covered_cell_counts[lev] = covered_mask[lev].sum(0, 0);
        interface_cell_counts[lev] = interface_mask[lev].sum(0, 0);
    }
}

void AmrCoreLBM::BuildBoundaryWorkBoxes() {
    for (int lev = 0; lev <= finest_level; ++lev) {
        const BoxArray& ba = f_old[lev].boxArray();
        const Box domain = Geom(lev).Domain();
        const auto is_periodic = Geom(lev).isPeriodicArray();
        auto& level_boundary_boxes = boundary_work_boxes[lev];
        level_boundary_boxes.resize(ba.size());

        for (int ibox = 0; ibox < ba.size(); ++ibox) {
            const Box& valid_box = ba[ibox];
            BoxList boundary_faces;

            for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
                if (is_periodic[dir]) {
                    continue;
                }

                const int lo = domain.smallEnd(dir);
                const int hi = domain.bigEnd(dir);
                if (valid_box.smallEnd(dir) == lo) {
                    Box face = valid_box;
                    face.setSmall(dir, lo);
                    face.setBig(dir, lo);
                    boundary_faces.push_back(face);
                }
                if (hi != lo && valid_box.bigEnd(dir) == hi) {
                    Box face = valid_box;
                    face.setSmall(dir, hi);
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
    if (cf_interp_mode >= 2) {
        for (int lev = 1; lev <= finest_level; ++lev) {
            BuildDirectInterpolationCache(lev);
        }
    } else if (cf_interp_mode == 0) {
        BuildInterpScaleWorkBoxes();
    }
}

void AmrCoreLBM::BuildInterpScaleWorkBoxes() {
    // 用于插值优化
    /*     整体数据关系
    fine ghost 待填充区域
            ↓ TheFPinfo
    fpc.ba_crse_patch
            ↓ 周期平移
    needed_crse_box + shift
            ↓ 与粗层 BoxArray 求交
    (index_of_coarse_box, intersection_box)
            ↓ 去重并缓存
    interp_scale_work_boxes[lev][coarse_box_id] */
    for (int lev = 0; lev < finest_level; ++lev) {
        const int fine_lev = lev + 1;
        const BoxArray& crse_ba = f_old[lev].boxArray();
        auto& level_work_boxes = interp_scale_work_boxes[lev];
        level_work_boxes.resize(crse_ba.size());

        const auto& coarsener = cell_bilinear_interp.BoxCoarsener(refRatio(lev)); // 区域转换器,根据需要填充的fine区域，计算对应的coarse区域
        const auto& fpc = FabArrayBase::TheFPinfo(
            f_old[fine_lev], f_old[fine_lev], amrex::IntVect(nghost), coarsener,
            Geom(fine_lev), Geom(lev), nullptr); // 获取FillPatch几何信息, 会减去能够由同层 fine Box 填充的区域

        const auto periodic_shifts = Geom(lev).periodicity().shiftIntVect();
        amrex::Vector<std::pair<int, Box>> intersections; // 这是一个临时交集结果容器, 后面会调用

        for (int ibox = 0; ibox < fpc.ba_crse_patch.size(); ++ibox) { // ba_crse_patch表示为了填充细层 nghost 层 ghost cell，经过同层 fine 数据覆盖后，仍然需要从粗层插值的那些区域，在粗网格索引空间中对应哪些 Box。
            const Box& needed_crse_box = fpc.ba_crse_patch[ibox];
            for (const IntVect& shift : periodic_shifts) {
                crse_ba.intersections(needed_crse_box + shift, intersections); // 它检查平移后的所需区域与粗层所有 valid Box 的交集，并将这些交集存储在 intersections 容器中。
                for (const auto& intersection : intersections) {
                    level_work_boxes[intersection.first].push_back(intersection.second);
                }
            }
        }

        for (auto& work_boxes_for_one_coarse_box : level_work_boxes) {
            BoxList box_list;

            for (const Box& work_box : work_boxes_for_one_coarse_box) {
                box_list.push_back(work_box);
            }

            BoxList disjoint_boxes = amrex::removeOverlap(box_list);

            work_boxes_for_one_coarse_box.assign(
                disjoint_boxes.begin(),
                disjoint_boxes.end());
        }
    }
}

void AmrCoreLBM::BuildRestrictionCache() {
    for (int lev = 0; lev < finest_level; ++lev) {
        BoxArray coarse_from_fine =
            amrex::coarsen(f_old[lev + 1].boxArray(), refRatio(lev));

        if (average_mode == 1) {
            average_down_buffer[lev].define(
                coarse_from_fine, f_old[lev + 1].DistributionMap(), Q, 0);
        }
        if (average_mode < 2) {
            continue;
        }

        BoxList interface_boxes;
        Vector<int> interface_owners;
        Vector<int> fine_box_indices;
        const Box domain = Geom(lev).Domain();
        const auto& periodicity = Geom(lev).periodicity();
        const auto& fine_dm = f_old[lev + 1].DistributionMap();

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
            average_interface_fine_box[lev] = std::move(fine_box_indices);
        }

        if (cf_mask_mode != 0) {
            // 几何方法构造的工作 Box 必须与逐单元 interface mask 覆盖完全相同的父单元，
            // 否则平均下传过程可能遗漏或重复处理单元。
            const Long restriction_interface_cells =
                average_interface_fine_box[lev].empty()
                    ? 0
                    : average_interface_buffer[lev].boxArray().numPts();
            AMREX_ALWAYS_ASSERT(restriction_interface_cells == interface_cell_counts[lev]);
            amrex::Print() << "average_interface_observe: lev=" << lev
                           << " cells=" << restriction_interface_cells
                           << " boxes=" << average_interface_fine_box[lev].size() << '\n';
        }
    }
}

void AmrCoreLBM::FindCentre() {
    // amrex::AllPrint()<<"FindCentre "<<std::endl;

    for (int p_num = 0; p_num < particle_num; p_num++) {
        points[p_num] = particles[p_num]->ReturnCentre();
        // amrex::Print() << "id " << p_num << "'s z_positon is " << points[p_num][2] << std::endl;
    }
}

//********************************************************************//
//                           lbm  function                            //
//********************************************************************//

void AmrCoreLBM::ComputeMacroLevel(int lev) {
    amrex::MultiFab& f_old_lev = f_old[lev];
    amrex::MultiFab& rho_lev = density[lev];
    amrex::MultiFab& u_lev = velocity[lev];

    for (MFIter mfi(f_old_lev, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const Box& bx = mfi.growntilebox(nghost);
        Array4<Real> const& fold = f_old_lev.array(mfi);
        Array4<Real> const& rho = rho_lev.array(mfi);
        Array4<Real> const& u = u_lev.array(mfi);

        amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
            compute_macro(i, j, k, fold, rho, u);
        });
    }
}

void AmrCoreLBM::ComputeMacro() {
    for (int lev = 0; lev <= finest_level; lev++) {
        ComputeMacroLevel(lev);
    }
}

void AmrCoreLBM::ComputeVorticityLevel(int lev) {
    // amrex::AllPrint()<<"ComputeVorticityLevel on " << lev <<std::endl;

    amrex::MultiFab& f_old_lev = f_old[lev];
    amrex::MultiFab& u_lev = velocity[lev];
    amrex::MultiFab& vort_lev = vorticity[lev];
    amrex::Real dt = Geom(lev).CellSizeArray()[0];

    for (MFIter mfi(f_old_lev, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
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
    amrex::MultiFab& f_old_lev = f_old[lev];
    amrex::MultiFab& u_lev = velocity[lev];
    amrex::MultiFab& shear_lev = shear[lev];
    amrex::Real dt = Geom(lev).CellSizeArray()[0];

    for (MFIter mfi(f_old_lev, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
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
    // amrex::AllPrint()<<"AverageDownValidLevel from " << lev+1 << " to " << lev <<std::endl;
    // amrex::average_down(f_old[lev+1], f_old[lev], geom[lev+1], geom[lev],0, Q, refRatio(lev));

    amrex::MultiFab& fine_mf = f_old[lev + 1];
    amrex::MultiFab& crse_mf = f_old[lev];

    MultiFab fine_boundary_data(fine_mf.boxArray(), fine_mf.DistributionMap(), Q, 0); // 能不能用f_new减少内存消耗
    MultiFab::Copy(fine_boundary_data, fine_mf, 0, 0, Q, 0);

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

void AmrCoreLBM::AverageDownValid() {
    // interface-only 时间推进不会更新深层 covered 粗单元；regrid 可能重新暴露这些单元，
    // 因此重网格前必须先将全部细层 valid 数据完整同步到粗层父单元。
    for (int lev = finest_level - 1; lev >= 0; --lev) {
        AverageDownValidLevel(lev, 1);
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
    // amrex::AllPrint()<<"AverageDownGhostLevel from " << lev+1 << " to " << lev <<std::endl;

    if (lev >= finest_level) {
        return;
    }

    amrex::MultiFab& fine_mf = f_old[lev + 1];
    amrex::MultiFab& fine_scratch = f_new[lev + 1];
    amrex::MultiFab& crse_mf = f_old[lev];

    const IntVect ratio = refRatio(lev);
    // 平均前先将细层非平衡分布函数转换到粗层松弛时间对应的尺度。
    const Real scale = 2.0 * tau[lev] / tau[lev + 1];
    const Long children_per_parent = ratio[0] * ratio[1] * ratio[2];

    ScopedPerfTimer avgdown_timer(perf_stats.average_down);

    if (average_mode == 0) {
        // 全区域分步基准路径：复制 -> 可选的原位缩放 -> AMReX 通用平均。
        // 当前层已完成 SwapLevel，因此此处可以安全地复用 f_new 作为临时缓冲区。
        {
            ScopedPerfTimer copy_timer(perf_stats.average_copy);
            MultiFab::Copy(fine_scratch, fine_mf, 0, 0, Q, 0);
        }
        if (is_scale) {
            ScopedPerfTimer scale_timer(perf_stats.average_scale);
            for (MFIter mfi(fine_scratch, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
                const Box bx = mfi.tilebox();
                const Array4<Real>& scratch = fine_scratch.array(mfi);
                perf_stats.average_scale_cells += bx.numPts();
                amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {
                    average_scale(i, j, k, scratch, scale);
                });
            }
        }
        {
            ScopedPerfTimer restrict_timer(perf_stats.average_restrict);
            amrex::average_down(fine_scratch, crse_mf, 0, Q, ratio);
        }
        return;
    }

    if (average_mode >= 2) {
        // 模式 2/3 使用完全相同的缓存父单元区域，因此二者的耗时对比只改变算法组织，
        // 不会改变实际处理的交界单元数量。
        MultiFab& interface_result = average_interface_buffer[lev];
        const Vector<int>& fine_box_indices = average_interface_fine_box[lev];
        if (fine_box_indices.empty()) {
            return;
        }

        AMREX_ALWAYS_ASSERT(interface_result.size() == fine_box_indices.size());

        if (average_mode == 2) {
            // 交界区域分步基准路径。细化稀疏父 Box 后，恰好得到参与这些粗父单元
            // 平均计算的 2x2x2 个细层子单元。
            {
                ScopedPerfTimer copy_timer(perf_stats.average_copy);
                for (MFIter mfi(interface_result, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
                    const int fine_index = fine_box_indices[mfi.index()];
                    const Box fine_box = amrex::refine(mfi.tilebox(), ratio); // 将当前粗层 interface Box 映射到对应的细层索引区域
                    const Array4<const Real>& fine = fine_mf.const_array(fine_index);
                    const Array4<Real>& scratch = fine_scratch.array(fine_index);
                    amrex::ParallelFor(
                        fine_box, Q,
                        [=] AMREX_GPU_DEVICE(int i, int j, int k, int q) noexcept {
                            scratch(i, j, k, q) = fine(i, j, k, q);
                        });
                }
            }

            if (is_scale) {
                ScopedPerfTimer scale_timer(perf_stats.average_scale);
                for (MFIter mfi(interface_result, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
                    const int fine_index = fine_box_indices[mfi.index()];
                    const Box fine_box = amrex::refine(mfi.tilebox(), ratio);
                    const Array4<Real>& scratch = fine_scratch.array(fine_index);
                    perf_stats.average_scale_cells += fine_box.numPts();
                    amrex::ParallelFor(
                        fine_box, [=] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {
                            average_scale(i, j, k, scratch, scale);
                        });
                }
            }

            {
                ScopedPerfTimer restrict_timer(perf_stats.average_restrict);
                for (MFIter mfi(interface_result, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
                    const int fine_index = fine_box_indices[mfi.index()];
                    const Box bx = mfi.tilebox();
                    const Array4<const Real>& scratch = fine_scratch.const_array(fine_index);
                    const Array4<Real>& coarse = interface_result.array(mfi);
                    perf_stats.average_parent_cells += bx.numPts();
                    amrex::ParallelFor(
                        bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {
                            average_down_lbm(i, j, k, coarse, scratch, ratio);
                        });
                }
            }
        } else {
            // 交界区域融合路径：不再于多个 kernel 之间写回完整的缩放后 DDF 中间数据。
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
                            const Long icell = static_cast<Long>(blockIdx.x) * warps_per_block +
                                               warp_in_block;
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
        return;
    }

    // 模式 1 是全区域融合基准，用于核对 interface-only 模式的数值结果。
    MultiFab& coarse_from_fine = average_down_buffer[lev];

    AMREX_ALWAYS_ASSERT(coarse_from_fine.boxArray() == amrex::coarsen(fine_mf.boxArray(), ratio));
    AMREX_ALWAYS_ASSERT(coarse_from_fine.DistributionMap() == fine_mf.DistributionMap());

    {
        ScopedPerfTimer fused_timer(perf_stats.average_fused);
        for (MFIter mfi(coarse_from_fine, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
            const Box bx = mfi.tilebox();
            const Array4<const Real>& fine = fine_mf.const_array(mfi);
            const Array4<Real>& coarse = coarse_from_fine.array(mfi);
            perf_stats.average_parent_cells += bx.numPts();

            if (is_scale) {
                perf_stats.average_scale_cells += bx.numPts() * ratio[0] * ratio[1] * ratio[2];
#ifdef AMREX_USE_CUDA
                // 全区域与稀疏区域的融合模式采用相同的 q-lane warp 映射。
                constexpr int threads_per_block = 256;
                constexpr int warp_size = 32;
                constexpr int warps_per_block = threads_per_block / warp_size;
                const Long ncells = bx.numPts();
                const int nblocks = static_cast<int>((ncells + warps_per_block - 1) /
                                                     warps_per_block);
                const auto lo = amrex::lbound(bx);
                const int nx = bx.length(0);
                const int ny = bx.length(1);

                amrex::launch<threads_per_block>(
                    nblocks, amrex::Gpu::Device::gpuStream(),
                    [=] AMREX_GPU_DEVICE() noexcept {
                        const int lane = threadIdx.x % warp_size;
                        const int warp_in_block = threadIdx.x / warp_size;
                        const Long icell = static_cast<Long>(blockIdx.x) * warps_per_block +
                                           warp_in_block;
                        if (icell < ncells) {
                            const int i = lo.x + static_cast<int>(icell % nx);
                            const Long yz = icell / nx;
                            const int j = lo.y + static_cast<int>(yz % ny);
                            const int k = lo.z + static_cast<int>(yz / ny);
                            average_down_lbm_scaled_warp(i, j, k, lane, coarse, fine, ratio,
                                                         scale);
                        }
                    });
#else
                amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {
                    average_down_lbm_scaled(i, j, k, coarse, fine, ratio, scale);
                });
#endif
            } else {
                amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {
                    average_down_lbm(i, j, k, coarse, fine, ratio);
                });
            }
        }
    }
    {
        ScopedPerfTimer copyback_timer(perf_stats.average_copyback);
        crse_mf.ParallelCopy(coarse_from_fine, 0, 0, Q);
    }
}

void AmrCoreLBM::AverageDownGhost() {
}

void AmrCoreLBM::FillGhostLevel(int lev, amrex::Real time, bool is_scale) {
    ScopedPerfTimer timer(perf_stats.interp);
    ++perf_stats.fillghost_calls;
    amrex::MultiFab& f_old_lev = f_old[lev];

    if (is_scale) {
        FillDdfPatch(lev, time, f_old_lev);
    } else {
        FillPatch(lev, time, f_old_lev);
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
    // amrex::AllPrint()<<"FillForceGhostLevel on " << lev <<std::endl;

    amrex::MultiFab& force_lev = force[lev];

    if (lev == 0) {
        force_lev.FillBoundary(geom[lev].periodicity());
    } else {
        // 填充c-f边界
        force_lev.FillBoundary(geom[lev].periodicity()); // 填充同等级
    }
}

void AmrCoreLBM::CommunicateLevel(int lev) {
    ScopedPerfTimer timer(perf_stats.comm);
    amrex::MultiFab& f_old_lev = f_old[lev];
    f_old_lev.FillBoundary(geom[lev].periodicity());
}

void AmrCoreLBM::Boundary(int lev) {
    ScopedPerfTimer timer(perf_stats.boundary);
    // amrex::AllPrint()<<"Boundary on " << lev <<std::endl;

    int right = Geom(lev).Domain().length(0) - 1;
    int back = Geom(lev).Domain().length(1) - 1;
    int up = Geom(lev).Domain().length(2) - 1;
    amrex::IntVect hi{right, back, up};
    const auto is_periodic = Geom(lev).isPeriodicArray();

    amrex::MultiFab& f_old_lev = f_old[lev];
    amrex::MultiFab& f_new_lev = f_new[lev];

    for (MFIter mfi(f_old_lev, false); mfi.isValid(); ++mfi) {
        const Array4<Real>& fold = f_old_lev.array(mfi);
        const Array4<Real>& fnew = f_new_lev.array(mfi);
        perf_stats.boundary_full_cells += mfi.tilebox().numPts();

        for (const Box& bx : boundary_work_boxes[lev][mfi.index()]) { // 用 mfi.index() 得到该 Box 的全局编号
            perf_stats.boundary_launch_cells += bx.numPts();
            amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                fill_boundary(i, j, k, fold, fnew, hi, is_periodic);
            });
        }
    }
}

void AmrCoreLBM::Collide(int lev, int n) {
    ScopedPerfTimer timer(perf_stats.collide);
    // amrex::AllPrint()<<"Collide on " << lev <<std::endl;

    int right = Geom(lev).Domain().length(0) - 1;
    int back = Geom(lev).Domain().length(1) - 1;
    int up = Geom(lev).Domain().length(2) - 1;
    amrex::IntVect hi{right, back, up};

    amrex::MultiFab& f_old_lev = f_old[lev];
    amrex::MultiFab& shear_lev = shear[lev];
    amrex::MultiFab& force_lev = force[lev];
    amrex::Real dt = Geom(lev).CellSizeArray()[0];
    amrex::Real tau_lev = tau[lev];
    const amrex::Real omega_lev = 1.0 / tau_lev;
    const Box domain = Geom(lev).Domain();
    const bool has_fine_level = (lev < finest_level);
    AMREX_ALWAYS_ASSERT(n <= cf_interface_mask_nghost);

    for (MFIter mfi(f_old_lev, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const auto bx = mfi.growntilebox(n) & domain;
        const Array4<Real>& fold = f_old_lev.array(mfi);
        const Array4<Real>& s = shear_lev.array(mfi);
        const Array4<Real>& Ft = force_lev.array(mfi);

        // collide_mode 在 host 侧选择 kernel，避免把基线和优化路径编译进
        // 同一个 device kernel，否则不同路径的寄存器需求会彼此干扰。
        const auto launch_active = [&](const Box& launch_box) {
            if (collide_mode == 0) {
                amrex::ParallelFor(launch_box, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                    collide(i, j, k, fold, s, Ft, tau_lev, dt, hi);
                });
            } else {
                amrex::ParallelFor(launch_box, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                    collide_bgk_register(i, j, k, fold, omega_lev);
                });
            }
        };

        const auto launch_masked = [&](const Box& launch_box,
                                       const Array4<const int>& covered,
                                       const Array4<const int>& interface) {
            if (collide_mode == 0) {
                amrex::ParallelFor(launch_box, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                    if (covered(i, j, k) != 0 && interface(i, j, k) == 0) {
                        return;
                    }
                    collide(i, j, k, fold, s, Ft, tau_lev, dt, hi);
                });
            } else {
                amrex::ParallelFor(launch_box, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                    if (covered(i, j, k) != 0 && interface(i, j, k) == 0) {
                        return;
                    }
                    collide_bgk_register(i, j, k, fold, omega_lev);
                });
            }
        };

        if (has_fine_level && cf_mask_mode == 1) {
            const Array4<const int>& covered = covered_mask[lev].const_array(mfi);
            const Array4<const int>& interface = interface_mask[lev].const_array(mfi);

            launch_masked(bx, covered, interface);
        } else {
            launch_active(bx);
        }
    }
}

void AmrCoreLBM::Stream(int lev, int n) {
    ScopedPerfTimer timer(perf_stats.stream);
    // amrex::AllPrint()<<"Stream on " << lev <<std::endl;
    AMREX_ALWAYS_ASSERT(n >= 1);
    AMREX_ALWAYS_ASSERT(n - 1 <= cf_covered_mask_nghost);

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

// void AmrCoreLBM::SwapLevel(int lev, int n)
// {
//     amrex::MultiFab& f_old_lev = f_old[lev];
//     amrex::MultiFab& f_new_lev = f_new[lev];

//     for(MFIter mfi(f_old_lev, TilingIfNotGPU()); mfi.isValid(); ++mfi)
//     {
//         const auto bx = mfi.growntilebox(n);
//         const Array4<Real>& fold = f_old_lev.array(mfi);
//         const Array4<Real>& fnew = f_new_lev.array(mfi);

//         amrex::ParallelFor(bx, [=]AMREX_GPU_DEVICE(int i, int j, int k)
//         {
//             swap_ddf(i, j, k, fold, fnew);
//         });
//     }
// }

void AmrCoreLBM::SwapLevel(int lev, int n) {
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
        particles[i] = std::make_unique<LagrangeParticleContainer>(this, points[i], i);
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
    // amrex::AllPrint()<<"ComputeParticle on " << lev <<std::endl;
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
    // amrex::AllPrint()<< "RedistributeParticle" << std::endl;
    for (int i = 0; i < particle_num; i++) {
        particles[i]->Redistribute();
    }
}

void AmrCoreLBM::InitCpPoint(int lev) {
    for (int i = 0; i < particle_num; i++) {
        particlesCp[i] = std::make_unique<AuxiliaryPointContainer>(this, points[i], i);
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
    // amrex::AllPrint()<<"MoveParticle on " << lev <<std::endl;
    for (int i = 0; i < particle_num; i++) {
        particles[i]->MoveParticle(lev, cur_time);
        particles[i]->Redistribute();
    }
}

//********************************************************************//
//                     Pure virtual function                          //
//********************************************************************//
void AmrCoreLBM::MakeNewLevelFromCoarse(int lev, amrex::Real time, const amrex::BoxArray& ba,
                                        const amrex::DistributionMapping& dm) // 暂时用不到
{
    // amrex::AllPrint()<<"MakeNewLevelFromCoarse on " << lev <<std::endl;

    if (lev == 0) {
        amrex::Abort("Cannot construct level 0 from a coarser level.");
    }

    amrex::MultiFab& u_lev = velocity.at(lev);
    amrex::MultiFab& rho_lev = density.at(lev);
    amrex::MultiFab& vort_lev = vorticity.at(lev);
    amrex::MultiFab& force_lev = force.at(lev);
    amrex::MultiFab& shear_lev = shear.at(lev);
    amrex::MultiFab& f_new_lev = f_new.at(lev);
    amrex::MultiFab& f_old_lev = f_old.at(lev);

    u_lev.define(ba, dm, AMREX_SPACEDIM, nghost);
    rho_lev.define(ba, dm, 1, nghost);
    vort_lev.define(ba, dm, 2, nghost); // 改成两个，分别存vort和q
    force_lev.define(ba, dm, AMREX_SPACEDIM, nghost);
    shear_lev.define(ba, dm, 1, nghost);
    f_new_lev.define(ba, dm, Q, nghost);
    f_old_lev.define(ba, dm, Q, nghost);

    FillCoarsePatch(lev, time, f_old_lev);
}
void AmrCoreLBM::RemakeLevel(int lev, amrex::Real time, const amrex::BoxArray& ba,
                             const amrex::DistributionMapping& dm) {
    // amrex::AllPrint()<<"ReMakeLevel on " << lev <<std::endl;

    amrex::MultiFab new_state(ba, dm, Q, nghost);
    amrex::MultiFab old_state(ba, dm, Q, nghost);
    amrex::MultiFab u_new(ba, dm, AMREX_SPACEDIM, nghost); // 什么用,要初始化吗
    amrex::MultiFab rho_new(ba, dm, 1, nghost);
    amrex::MultiFab vort_new(ba, dm, 2, nghost);
    amrex::MultiFab force_new(ba, dm, AMREX_SPACEDIM, nghost);
    amrex::MultiFab shear_new(ba, dm, 1, nghost);

    {
        ScopedPerfTimer timer(perf_stats.interp_regrid_fill);
        ++perf_stats.interp_regrid_fill_calls;
        FillDdfPatch(lev, time, old_state);
    }
    // FillPatch(lev, time, old_state);

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
void AmrCoreLBM::ClearLevel(int lev) {
    // amrex::AllPrint()<<"ClearLevel on " << lev <<std::endl;

    f_old[lev].clear();
    f_new[lev].clear();
    velocity[lev].clear();
    vorticity[lev].clear();
    density[lev].clear();
    shear[lev].clear();
    force[lev].clear();
}
void AmrCoreLBM::MakeNewLevelFromScratch(int lev, amrex::Real time, const amrex::BoxArray& ba,
                                         const amrex::DistributionMapping& dm) {
    amrex::MultiFab& u_lev = velocity.at(lev);
    amrex::MultiFab& rho_lev = density.at(lev);
    amrex::MultiFab& vort_lev = vorticity.at(lev);
    amrex::MultiFab& force_lev = force.at(lev);
    amrex::MultiFab& shear_lev = shear.at(lev);
    amrex::MultiFab& f_new_lev = f_new.at(lev);
    amrex::MultiFab& f_old_lev = f_old.at(lev);

    u_lev.define(ba, dm, AMREX_SPACEDIM, nghost);
    rho_lev.define(ba, dm, 1, nghost);
    vort_lev.define(ba, dm, 2, nghost);
    force_lev.define(ba, dm, AMREX_SPACEDIM, nghost);
    shear_lev.define(ba, dm, 1, nghost);
    f_new_lev.define(ba, dm, Q, nghost);
    f_old_lev.define(ba, dm, Q, nghost);

    force_lev.setVal(0.0, nghost); // 在这里归零会不会好一点
    shear_lev.setVal(0.0, nghost);
    vort_lev.setVal(0.0, nghost);

    for (MFIter mfi(f_old_lev, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const Box& bx = mfi.growntilebox(nghost);
        Array4<Real> const& fold = f_old_lev.array(mfi);
        Array4<Real> const& fnew = f_new_lev.array(mfi);

        amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
            init_fluid(i, j, k, fold, fnew);
        });
    }
}

/*************************************第3版*ErrorEst***************************************/
void AmrCoreLBM::ErrorEst(int lev, amrex::TagBoxArray& tags, amrex::Real time, int ngrow) {
    // amrex::AllPrint()<<"ErrorEst on " << lev <<std::endl;

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

    const MultiFab& f_old_lev = f_old[lev];
    const MultiFab& vort_lev = vorticity[lev];

    amrex::IntVect lo1 = static_lo[lev];
    amrex::IntVect hi1 = static_hi[lev];

    amrex::IntVect lo2 = static_lo[lev + max_ref_level + 1];
    amrex::IntVect hi2 = static_hi[lev + max_ref_level + 1];

    const auto geomdata = geom[lev].data();
    amrex::Gpu::DeviceVector<amrex::RealVect> points_d = convertToDeviceVector(points);

    for (MFIter mfi(f_old_lev, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
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

        amrex::UtilCreateCleanDirectory(out_chkname, false);

        for (int lev = 0; lev <= finest_level; ++lev) {
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

    if (isIOP) {
        std::ofstream header(out_chkname + "/Header", std::ios::out | std::ios::trunc);
        if (!header.is_open()) {
            amrex::Print() << "[Checkpoint][ERROR] Cannot open '" << out_chkname
                           << "/Header' for writing. Abort checkpoint write for this step.\n";
            return;
        }
        header.precision(17);
        header << "LBMCheckpoint" << '\n';
        header << step << '\n';
        header << time << '\n';
        header << finest_level << '\n';
        for (int lev = 0; lev <= finest_level; ++lev) {
            boxArray(lev).writeOn(header);
            header << '\n';
            DistributionMap(lev).writeOn(header);
            header << '\n';
        }
        header.flush();
    }

    for (int lev = 0; lev <= finest_level; ++lev) {
        VisMF::Write(f_old[lev], MultiFabFileFullPrefix(lev, out_chkname, level_prefix, "f_old"));
        VisMF::Write(f_new[lev], MultiFabFileFullPrefix(lev, out_chkname, level_prefix, "f_new"));
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
        DIR* dir = opendir(".");
        if (dir) {
            struct dirent* entry;
            int removed_count = 0;
            const auto is_chk_dir = [&](const std::string& name) {
                if (name == out_chkname) {
                    return false;
                }
                if (name.rfind(params_.chk_prefix, 0) != 0) {
                    return false;
                }
                if (name.size() <= params_.chk_prefix.size()) {
                    return false;
                }
                return std::all_of(name.begin() + params_.chk_prefix.size(), name.end(),
                                   [](unsigned char ch) { return std::isdigit(ch); });
            };

            while ((entry = readdir(dir)) != nullptr) {
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
        } else {
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
        label == "LBMCheckpoint",
        "Invalid DDF reference checkpoint: " + checkpoint_path);

    std::string line;
    std::getline(header, line); // step
    std::getline(header, line); // time
    int reference_finest = -1;
    header >> reference_finest;
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        reference_finest == finest_level,
        "DDF comparison requires identical finest levels");

    constexpr const char* level_prefix = "Level_";
    Real global_linf = 0.0;
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

        const MultiFab& current = f_old[lev];
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            reference.nComp() == Q,
            "DDF comparison found a reference component-count mismatch");
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            reference.boxArray() == current.boxArray(),
            "DDF comparison requires identical BoxArrays at every level");

        MultiFab reference_on_current(
            current.boxArray(), current.DistributionMap(), Q, 0);
        reference_on_current.ParallelCopy(
            reference, 0, 0, Q, IntVect(0), IntVect(0));

        MultiFab difference(
            current.boxArray(), current.DistributionMap(), Q, 0);
        MultiFab::Copy(difference, current, 0, 0, Q, 0);
        MultiFab::Subtract(
            difference, reference_on_current, 0, 0, Q, 0);

        const Real level_linf =
            difference.norm0(0, Q, IntVect(0));
        const Real level_diff_l2 = difference.norm2(0, Q);
        const Real level_ref_l2 = reference_on_current.norm2(0, Q);
        const Real level_rel_l2 =
            level_ref_l2 > 0.0 ? level_diff_l2 / level_ref_l2 : 0.0;

        global_linf = std::max(global_linf, level_linf);
        global_diff_l2_sq += level_diff_l2 * level_diff_l2;
        global_ref_l2_sq += level_ref_l2 * level_ref_l2;

        amrex::Print() << "ddf_norm_level: lev=" << lev
                       << " valid_cells=" << current.boxArray().numPts()
                       << " linf=" << level_linf
                       << " l2=" << level_diff_l2
                       << " ref_l2=" << level_ref_l2
                       << " rel_l2=" << level_rel_l2 << '\n';

        for (int q = 0; q < Q; ++q) {
            const Real linf = difference.norminf(q);
            const Real diff_l2 = difference.norm2(q);
            const Real ref_l2 = reference_on_current.norm2(q);
            const Real rel_l2 =
                ref_l2 > 0.0 ? diff_l2 / ref_l2 : 0.0;
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
    std::istringstream is(header_str);

    std::string label;
    std::getline(is, label);
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(label == "LBMCheckpoint", "Invalid checkpoint header");

    {
        std::string tmp;
        std::getline(is, tmp);
    }
    {
        std::string tmp;
        std::getline(is, tmp);
    }

    int finest_in_file;
    is >> finest_in_file;
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

    SetFinestLevel(finest_in_file);
    for (int lev = 0; lev <= finest_level; ++lev) {
        SetBoxArray(lev, ba_file[lev]);
        SetDistributionMap(lev, dm_file[lev]);
    }

    const std::string level_prefix = "Level_";
    for (int lev = 0; lev <= finest_level; ++lev) {
        ClearLevel(lev);
        f_old[lev].define(boxArray(lev), DistributionMap(lev), Q, nghost);
        f_new[lev].define(boxArray(lev), DistributionMap(lev), Q, nghost);
        velocity[lev].define(boxArray(lev), DistributionMap(lev), AMREX_SPACEDIM, nghost);
        density[lev].define(boxArray(lev), DistributionMap(lev), 1, nghost);
        vorticity[lev].define(boxArray(lev), DistributionMap(lev), 2, nghost);
        shear[lev].define(boxArray(lev), DistributionMap(lev), 1, nghost);
        force[lev].define(boxArray(lev), DistributionMap(lev), AMREX_SPACEDIM, nghost);

        VisMF::Read(f_old[lev], MultiFabFileFullPrefix(lev, chkname, level_prefix, "f_old"));
        VisMF::Read(f_new[lev], MultiFabFileFullPrefix(lev, chkname, level_prefix, "f_new"));

        velocity[lev].setVal(0.0, nghost);
        density[lev].setVal(0.0, nghost);
        vorticity[lev].setVal(0.0, nghost);
        shear[lev].setVal(0.0, nghost);
        force[lev].setVal(0.0, nghost);
    }

    for (int i = 0; i < particle_num; ++i) {
        const std::string pname = "particles_" + std::to_string(i);
        particles[i].reset();
        particles[i] = std::make_unique<LagrangeParticleContainer>(this, points[i], i);
        particles[i]->Restart(chkname, pname);
    }

    RebuildCoarseFineCaches();
}
