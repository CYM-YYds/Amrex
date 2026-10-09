#include "AmrCoreLBM.H"
#include "AmrCoreLBM_detail.H"

#include <AMReX_ParIter.H>
#include <AMReX_PlotFileUtil.H>
#include <AMReX_Utility.H>
#include <AMReX_VisMF.H>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cctype>
#include <cmath>
#include "InitParticles.H"
#include "Kernels.H"

using namespace amrex;
using namespace Box3dDetail;

// 基础生命周期、参数、宏观量重建、输出、检查点与粒子耦合。

void AmrCoreLBM::ComputeMacroLevel(int lev) {
    if (stream_mode == 1) {
        const amrex::MultiFab& state = osi_state[lev];
        amrex::MultiFab& rho_lev = density[lev];
        amrex::MultiFab& u_lev = velocity[lev];
        const std::uint64_t phase = osi_phase[lev];

        for (MFIter mfi(state, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
            const Box& bx = mfi.tilebox();
            const Box ring_box = amrex::grow(mfi.validbox(), state.nGrowVect());
            const auto [fab, phase_shift] = OSI::MakeOsiFabContext(ring_box, phase);
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

AmrCoreLBM::AmrCoreLBM(amrex::Geometry const& level_0_geom, amrex::AmrInfo const& amr_info,
                       LbmGridParams grid)
    : AmrCore(level_0_geom, amr_info), grid_(grid) {
    ReadParameters();

    int nlevs_max = max_level + 1;

    f_new.resize(nlevs_max);
    f_old.resize(nlevs_max);
    osi_state.resize(nlevs_max);
    osi_phase.resize(nlevs_max, 0);
    osi_decode_boxes.resize(nlevs_max);
    osi_encode_boxes.resize(nlevs_max);
    osi_local_copy_tags.resize(nlevs_max);
    osi_interp_local_copy_tags.resize(nlevs_max);
    osi_remote_copy_tags.resize(nlevs_max);
    osi_mpi_pack_tags.resize(nlevs_max);
    osi_mpi_unpack_tags.resize(nlevs_max);
    osi_mpi_send_offsets.resize(nlevs_max);
    osi_mpi_recv_offsets.resize(nlevs_max);
    osi_mpi_send_counts.resize(nlevs_max);
    osi_mpi_recv_counts.resize(nlevs_max);
    osi_mpi_send_device.resize(nlevs_max);
    osi_mpi_recv_device.resize(nlevs_max);
    osi_mpi_send_host.resize(nlevs_max);
    osi_mpi_recv_host.resize(nlevs_max);
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
    const amrex::Real reynolds_number =
        U0 * grid_.x_length / grid_.viscosity;
    amrex::Print() << std::setw(15) << std::left << "  Re     =" << std::setw(10) << std::right << reynolds_number << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  cs2    =" << std::setw(10) << std::right << cs2 << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  p0     =" << std::setw(10) << std::right << p0 << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  Ma     =" << std::setw(10) << std::right << Ma << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  U0     =" << std::setw(10) << std::right << U0 << std::endl;
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
        ParmParse pp_performance("performance");
        pp_performance.query("report_int", params_.perf_report_int);
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            params_.perf_report_int > 0,
            "performance.report_int must be positive");
    }
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
        pp.query("collide_mode", collide_mode);
        if (collide_mode < 0 || collide_mode > 1) {
            amrex::Abort("lbm.collide_mode must be 0 or 1");
        }
        pp.query("stream_mode", stream_mode);
        if (stream_mode < 0 || stream_mode > 1) {
            amrex::Abort("lbm.stream_mode must be 0 (A-B) or 1 (stage-2 OSI)");
        }
        pp.query("osi_local_direct", osi_local_direct);
        pp.query("osi_parallel_copy", osi_parallel_copy);
        pp.query("osi_mpi_direct", osi_mpi_direct);
        pp.query("osi_mpi_device_direct", osi_mpi_device_direct);
        pp.query("osi_mpi_async_staging", osi_mpi_async_staging);
        pp.query("osi_mpi_pipeline_chunk_bytes", osi_mpi_pipeline_chunk_bytes);
        if (osi_mpi_pipeline_chunk_bytes < 0) {
            amrex::Abort("lbm.osi_mpi_pipeline_chunk_bytes must be non-negative");
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

    run_mode = stream_mode == 0
                   ? RunMode::CanonicalAB
                   : (osi_ab_check ? RunMode::OsiLockstep
                                   : RunMode::OsiProduction);
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
            const auto [fab, phase_shift] = OSI::MakeOsiFabContext(ring, phase);
            const auto src = state.const_array(mfi);
            const auto dst = canonical.array(mfi);
            amrex::ParallelFor(
                bx, Q,
                [=] AMREX_GPU_DEVICE(
                    int i, int j, int k, int q) noexcept {
                    const auto raw = OSI::osi_address(
                        {i, j, k},
                        {e[q][0], e[q][1], e[q][2]}, fab, phase_shift);
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

            if (osiReferenceEnabled()) {
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
