#include <AMReX_ParallelDescriptor.H>
#include <AMReX_ParmParse.H>
#include <AMReX_MultiFabUtil.H>
#include <AMReX_GpuAtomic.H>
#include <AMReX_PlotFileUtil.H>
#include <AMReX_VisMF.H>
#include <AMReX_PhysBCFunct.H>
#include <AMReX_MFIter.H>
#include <AMReX_ParIter.H>
#include <AMReX_Utility.H>

#include <fstream>
#include <sstream>
#include <cctype>
#include <dirent.h> // added for opendir/readdir/closedir
#include <set>
#include <map>
#include <algorithm>
#include <numeric>

#ifdef AMREX_MEM_PROFILING
#include <AMReX_MemProfiler.H>
#endif

#include "AmrCoreLBM.H"
#include "Kernels.H"

// IDF: 辅助函数和线性求解器
namespace {

// 稠密矩阵乘向量
void denseMatVec(const std::vector<amrex::Real>& A,
                 const std::vector<amrex::Real>& x,
                 std::vector<amrex::Real>& y) {
    int N = static_cast<int>(x.size());
    y.assign(N, 0.0);
    for (int i = 0; i < N; ++i) {
        const amrex::Real* row = &A[i * N];
        amrex::Real acc = 0.0;
        for (int j = 0; j < N; ++j)
            acc += row[j] * x[j];
        y[i] = acc;
    }
}

// LU 分解求矩阵逆（带部分主元选择）
static bool computeMatrixInverse(const std::vector<amrex::Real>& A,
                                 std::vector<amrex::Real>& A_inv,
                                 int N) {
    if (N <= 0)
        return false;

    // 创建增广矩阵 [A | I]
    std::vector<amrex::Real> aug(N * 2 * N, 0.0);
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            aug[i * 2 * N + j] = A[i * N + j];
        }
        aug[i * 2 * N + N + i] = 1.0;
    }

    // 高斯-约旦消元（带部分主元选择）
    for (int col = 0; col < N; ++col) {
        // 寻找主元
        int max_row = col;
        amrex::Real max_val = std::abs(aug[col * 2 * N + col]);
        for (int row = col + 1; row < N; ++row) {
            amrex::Real val = std::abs(aug[row * 2 * N + col]);
            if (val > max_val) {
                max_val = val;
                max_row = row;
            }
        }

        // 检查奇异性
        if (max_val < 1e-14) {
            if (amrex::ParallelDescriptor::IOProcessor()) {
                amrex::Print() << "[MatrixInverse] Warning: Matrix is singular at col "
                               << col << ", pivot=" << max_val << std::endl;
            }
            return false;
        }

        // 交换行
        if (max_row != col) {
            for (int j = 0; j < 2 * N; ++j) {
                std::swap(aug[col * 2 * N + j], aug[max_row * 2 * N + j]);
            }
        }

        // 归一化主元行
        amrex::Real pivot = aug[col * 2 * N + col];
        for (int j = 0; j < 2 * N; ++j) {
            aug[col * 2 * N + j] /= pivot;
        }

        // 消元
        for (int row = 0; row < N; ++row) {
            if (row != col) {
                amrex::Real factor = aug[row * 2 * N + col];
                for (int j = 0; j < 2 * N; ++j) {
                    aug[row * 2 * N + j] -= factor * aug[col * 2 * N + j];
                }
            }
        }
    }

    // 提取逆矩阵
    A_inv.resize(N * N);
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            A_inv[i * N + j] = aug[i * 2 * N + N + j];
        }
    }

    return true;
}

} // namespace

using namespace amrex;

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
    points[1] = {X + 2.0 * D, Y, Z};
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
    // (移除：误放在构造函数中的 checkpoint 写逻辑)
}

AmrCoreLBM::AmrCoreLBM() {
}
AmrCoreLBM::~AmrCoreLBM() = default;

//********************************************************************//
//                           help function                            //
//********************************************************************//
void AmrCoreLBM::PrintGridLayout(const amrex::Vector<amrex::BoxArray>& ba,
                                 const amrex::Vector<amrex::DistributionMapping>& dm) {
    for (int lev = 0; lev < ba.size(); ++lev) {
        amrex::Print() << "[Level " << lev << "] nboxes = " << ba[lev].size() << "\n";
        const auto& pm = dm[lev].ProcessorMap();
        for (int b = 0; b < ba[lev].size(); ++b) {
            amrex::Print() << "  box[" << b << "] " << ba[lev][b]
                           << " -> rank " << pm[b] << "\n";
        }
    }
}

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
void AmrCoreLBM::PrintLbmParm() {
    amrex::Print() << "╔════════════════════════════════════════════════════════╗" << std::endl;
    amrex::Print() << "║               LBM Parameters                           ║" << std::endl;
    amrex::Print() << "╚════════════════════════════════════════════════════════╝" << std::endl;

    amrex::Print() << std::setw(15) << std::left << "  NX     =" << std::setw(10) << std::right << Geom(0).Domain().length(0) << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  NY     =" << std::setw(10) << std::right << Geom(0).Domain().length(1) << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  NZ     =" << std::setw(10) << std::right << Geom(0).Domain().length(2) << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  dx_0   =" << std::setw(10) << std::right << dx_0 << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  dx_min =" << std::setw(10) << std::right << dx_min << std::endl;
#if defined(Re)
    amrex::Print() << std::setw(15) << std::left << "  Re     =" << std::setw(10) << std::right << Re << std::endl;
#else
#if defined(Ret)
    amrex::Print() << std::setw(15) << std::left << "  Ret    =" << std::setw(10) << std::right << Ret << std::endl;
#endif
#if defined(ReB)
    amrex::Print() << std::setw(15) << std::left << "  ReB    =" << std::setw(10) << std::right << ReB << std::endl;
#endif
#endif
    amrex::Print() << std::setw(15) << std::left << "  cs2    =" << std::setw(10) << std::right << cs2 << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  p0     =" << std::setw(10) << std::right << p0 << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  Ma     =" << std::setw(10) << std::right << Ma << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  U0     =" << std::setw(10) << std::right << Uc << std::endl;

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
        // collect commonly used amr toggles
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
        int n = pp.countval("err");
        if (n > 0) {
            pp.getarr("err", err, 0, n);
        }
    }

    // Centralize checkpoint namespace parsing
    {
        ParmParse pp_ckpt("checkpoint");

        // Interval and begin_step
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
        std::string filename = "data/" + amrex::Concatenate("particle_data", step, 6) + ".dat";

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

void AmrCoreLBM::FillDdfPatch(int lev, amrex::Real time, amrex::MultiFab& mf) // 加入缩放
{
    // amrex::AllPrint()<<"FillDdfPatch from " << lev-1 << " to " << lev <<std::endl;

    Interpolater* mapper = &cell_cons_interp;

    amrex::MultiFab& f_new_lev_c = f_new[lev - 1]; // 用f_new当缓存容器
    amrex::MultiFab& f_old_lev_f = f_old[lev];     // 保持和传入的mf一致

    amrex::Vector<amrex::MultiFab*> cmf{&f_new_lev_c};
    amrex::Vector<amrex::MultiFab*> fmf{&f_old_lev_f};

    amrex::Vector<Real> ctime{time};
    amrex::Vector<Real> ftime{time};

    // 如果是粗网格插值到细网格valid,无论如何只需要操作粗网格就可以了。
    amrex::MultiFab& f_old_lev_c = f_old[lev - 1];
    amrex::Real scale = tau[lev] / tau[lev - 1] / 2.0;

    for (MFIter mfi(f_old_lev_c, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const auto bx = mfi.growntilebox(0); // 只需要粗网格的valid值就可以了

        const Array4<Real>& fold = f_old_lev_c.array(mfi);
        const Array4<Real>& fnew = f_new_lev_c.array(mfi);

        amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) { interp_scale(i, j, k, fold, fnew, scale); });
    }

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
    // amrex::Print()<<"..............."<<std::endl;
    // amrex::Print()<<"regrid begin..."<<std::endl;
    regrid(0, cur_time);
    // amrex::Print()<<"regrid end....."<<std::endl;
    // amrex::Print()<<"..............."<<std::endl;
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
void AmrCoreLBM::ComputeMacro() {
    for (int lev = 0; lev <= finest_level; lev++) {
        ComputeMacroLevel(lev);
    }
}

void AmrCoreLBM::ComputeMacroLevel(int lev) {
    amrex::MultiFab& f_old_lev = f_old[lev];
    amrex::MultiFab& rho_lev = density[lev];
    amrex::MultiFab& u_lev = velocity[lev];

    for (MFIter mfi(f_old_lev, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const Box& bx = mfi.growntilebox(nghost);
        Array4<Real> const& fold = f_old_lev.array(mfi);
        Array4<Real> const& rho = rho_lev.array(mfi);
        Array4<Real> const& u = u_lev.array(mfi);

        amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) { compute_macro(i, j, k, fold, rho, u); });
    }
}

void AmrCoreLBM::ComputeMacroSlice(int lev, int ix) {
    amrex::MultiFab& f_old_lev = f_old[lev];
    amrex::MultiFab& rho_lev = density[lev];
    amrex::MultiFab& u_lev = velocity[lev];

    for (MFIter mfi(f_old_lev, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const Box& vb = mfi.validbox();
        if (vb.smallEnd(0) > ix || vb.bigEnd(0) < ix) {
            continue;
        }

        // Build a 1-cell-thick slice box at i=ix intersected with this tile
        int jlo = vb.smallEnd(1);
        int jhi = vb.bigEnd(1);
        int klo = vb.smallEnd(2);
        int khi = vb.bigEnd(2);
        Box sb(IntVect(AMREX_D_DECL(ix, jlo, klo)), IntVect(AMREX_D_DECL(ix, jhi, khi)));

        Array4<Real> const& fold = f_old_lev.array(mfi);
        Array4<Real> const& rho = rho_lev.array(mfi);
        Array4<Real> const& u = u_lev.array(mfi);

        amrex::ParallelFor(sb, [=] AMREX_GPU_DEVICE(int i, int j, int k) { compute_macro(i, j, k, fold, rho, u); });
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

        amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) { compute_vorticity(i, j, k, u, vort, dt); });
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

        amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) { compute_shear(i, j, k, u, shear, dt); });
    }
}

void AmrCoreLBM::ComputeShear() {
    for (int lev = 0; lev <= finest_level; lev++) {
        ComputeShearLevel(lev);
    }
}

// 记录通道流在 x = NX/2 截面的时间平均速度廓线 \bar{u}(y)
void AmrCoreLBM::RecordMeanVelocityProfile(int step) {
    // 仅在 50*FT ~ 100*FT 且每 1000 步采样
    if (!(step >= 50 * FT && step <= 100 * FT && step % 1000 == 0)) {
        // if (!(step >= 1000 && step <= 3000 && step % 1000 == 0)) {
        return;
    }

    const int lev = 0; // 只在 level 0 统计

    // 如需确保速度是从分布函数最新反演，可调用：ComputeMacroLevel(lev)
    // 若上游时间推进已更新 velocity[lev]，可跳过以节省开销。
    // ComputeMacroLevel(lev);

    const amrex::Geometry& geom = Geom(lev);
    const amrex::Box& domain = geom.Domain();
    const auto dx = geom.CellSizeArray();

    // x = NX/2 截面（使用索引空间定义，避免依赖宏）
    const int ix_mid = domain.smallEnd(0) + domain.length(0) / 2;
    // 仅对该切片做宏观反演（更省时）
    ComputeMacroSlice(lev, ix_mid);

    // y 在 [1, NH] 区间（排除壁面 y=0，索引空间：jy0+1 ~ jy0+ny_half）
    const int jy0 = domain.smallEnd(1);       // 最小的y值
    const int ny_half = domain.length(1) / 2; // NH = NY/2

    MultiFab& u_mf = velocity[lev];

    // 初始化全局累加器尺寸（首次进入时）
    if (!mean_initialized_) {
        mean_ny_half_ = ny_half;
        mean_u_sum_.assign(ny_half, 0.0); // 下标 0~ny_half-1 对应 y=1~NH
        mean_initialized_ = true;
    }
    amrex::Vector<Real> u_sum(ny_half, 0.0);
    amrex::Vector<int> z_cnt(ny_half, 0);

#ifdef AMREX_USE_GPU
    amrex::Gpu::DeviceVector<Real> u_sum_dev(ny_half, Real(0.0));
    amrex::Gpu::DeviceVector<int> z_cnt_dev(ny_half, 0);
    Real* u_sum_ptr = u_sum_dev.data();
    int* z_cnt_ptr = z_cnt_dev.data();
    const int ny_half_local = ny_half;
    const int jy_base = jy0 + 1;
#endif

    for (MFIter mfi(u_mf); mfi.isValid(); ++mfi) {
        const Box& bx = mfi.validbox();

        // 仅处理覆盖 ix_mid 截面的盒子
        if (bx.smallEnd(0) <= ix_mid && bx.bigEnd(0) >= ix_mid) {
            // 只统计 y=jy0+1 ~ jy0+ny_half
            const int jlo = std::max(bx.smallEnd(1), jy0 + 1);
            const int jhi = std::min(bx.bigEnd(1), jy0 + ny_half);

            if (jlo <= jhi) {
                const int klo = bx.smallEnd(2);
                const int khi = bx.bigEnd(2);

#ifdef AMREX_USE_GPU
                const Array4<Real const> u_arr = u_mf.const_array(mfi);
                Box slice(IntVect(AMREX_D_DECL(ix_mid, jlo, klo)), IntVect(AMREX_D_DECL(ix_mid, jhi, khi)));
                amrex::ParallelFor(slice, [=] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {
                    int jj = j - jy_base;
                    if (jj >= 0 && jj < ny_half_local) {
                        amrex::Gpu::Atomic::Add(&u_sum_ptr[jj], u_arr(i, j, k, 0));
                        amrex::Gpu::Atomic::Add(&z_cnt_ptr[jj], 1);
                    }
                });
#else
                const Array4<Real const>& u_arr = u_mf.const_array(mfi);
                for (int j = jlo; j <= jhi; ++j) {
                    const int jj = j - (jy0 + 1);
                    for (int k = klo; k <= khi; ++k) {
                        u_sum[jj] += u_arr(ix_mid, j, k, 0);
                        z_cnt[jj] += 1;
                    }
                }
#endif
            }
        }
    }

#ifdef AMREX_USE_GPU
    amrex::Gpu::Device::streamSynchronize();
    amrex::Gpu::copy(amrex::Gpu::deviceToHost, u_sum_dev.begin(), u_sum_dev.end(), u_sum.begin());
    amrex::Gpu::copy(amrex::Gpu::deviceToHost, z_cnt_dev.begin(), z_cnt_dev.end(), z_cnt.begin());
#endif

    // 全局归约（跨 MPI ranks），得到当前采样的截面平均（求和与计数）
    amrex::ParallelDescriptor::ReduceRealSum(u_sum.data(), ny_half);
    amrex::ParallelDescriptor::ReduceIntSum(z_cnt.data(), ny_half);

    for (int jj = 0; jj < ny_half; ++jj) {
        if (z_cnt[jj] > 0) {
            mean_u_sum_[jj] += (u_sum[jj] / static_cast<Real>(z_cnt[jj]));
        }
    }
    if (ParallelDescriptor::IOProcessor()) {
        mean_samples_ += 1;
    }

    // 仅在 100*FT 时刻（窗口末端）写一次最终平均文件
    if (step >= 50 * FT && step <= 100 * FT && step % 50000 == 0 && ParallelDescriptor::IOProcessor()) {
        const int nyh = mean_ny_half_;
        const int samples = std::max(1, mean_samples_);
        std::ofstream ofs("data/mean_velocity_profile_final.txt", std::ios::out);
        for (int jj = 0; jj < nyh; ++jj) {
            const int j = jy0 + 1 + jj; // y=jy0+1 ~ jy0+ny_half
            Real y_phys = geom.ProbLo(1) + (j + 0.5) * dx[1];
            Real u_bar = (samples > 0) ? (mean_u_sum_[jj] / static_cast<Real>(samples)) : 0.0;
            ofs << y_phys << " " << u_bar << "\n";
        }
        ofs.close();
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

            amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) { average_scale(i, j, k, fold, scale); });
        }
    }
    amrex::average_down(fine_boundary_data, crse_mf, 0, Q, refRatio(lev));
}

void AmrCoreLBM::AverageDownValid() {
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
    // amrex::AllPrint()<<"AverageDownGhostLevel from " << lev+1 << " to " << lev <<std::endl;

    amrex::MultiFab& fine_mf = f_old[lev + 1];
    amrex::MultiFab& crse_mf = f_old[lev];

    MultiFab fine_boundary_data(fine_mf.boxArray(), fine_mf.DistributionMap(), Q, 2); // 能不能用f_new减少内存消耗
    MultiFab::Copy(fine_boundary_data, fine_mf, 0, 0, Q, 2);

    if (is_scale) {
        amrex::Real scale = 2.0 * tau[lev] / tau[lev + 1];

        for (MFIter mfi(fine_boundary_data, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
            const auto bx = mfi.growntilebox(2);

            const Array4<Real>& fold = fine_boundary_data.array(mfi);

            amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) { average_scale(i, j, k, fold, scale); });
        }
    }

    amrex::average_down(fine_boundary_data, crse_mf, 0, Q, refRatio(lev));
}

void AmrCoreLBM::AverageDownGhost() {
}

void AmrCoreLBM::FillGhostLevel(int lev, amrex::Real time, bool is_scale) {
    amrex::MultiFab& f_old_lev = f_old[lev];

    if (is_scale) {
        FillDdfPatch(lev, time, f_old_lev);
    } else {
        FillPatch(lev, time, f_old_lev);
    }
}

/**
 * @brief 填充宏观速度场的 ghost cell 层
 * @param lev 网格层级
 * @param time 当前时间
 */
void AmrCoreLBM::FillMacroGhostLevel(int lev, amrex::Real time) {
    amrex::MultiFab& u_lev = velocity[lev]; // 获取当前层级的速度场多网格数据结构

    if (lev == 0) {
        u_lev.FillBoundary(geom[lev].periodicity()); // 对于最底层网格，只填充周期性边界
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
    amrex::MultiFab& f_old_lev = f_old[lev];
    f_old_lev.FillBoundary(geom[lev].periodicity());
}

void AmrCoreLBM::Boundary(int lev) {
    // amrex::AllPrint()<<"Boundary on " << lev <<std::endl;

    int right = Geom(lev).Domain().length(0) - 1;
    int back = Geom(lev).Domain().length(1) - 1;
    int up = Geom(lev).Domain().length(2) - 1;
    amrex::IntVect hi{right, back, up};

    amrex::MultiFab& f_old_lev = f_old[lev];
    amrex::MultiFab& f_new_lev = f_new[lev];

    for (MFIter mfi(f_old_lev, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const auto bx = mfi.growntilebox(nghost);
        const Array4<Real>& fold = f_old_lev.array(mfi);
        const Array4<Real>& fnew = f_new_lev.array(mfi);

        amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) { fill_boundary(i, j, k, fold, fnew, hi); });
    }
}

void AmrCoreLBM::Collide(int lev, int n) {
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

    for (MFIter mfi(f_old_lev, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const auto bx = mfi.growntilebox(n);
        const Array4<Real>& fold = f_old_lev.array(mfi);
        const Array4<Real>& s = shear_lev.array(mfi);
        const Array4<Real>& Ft = force_lev.array(mfi);

        amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
            // collide(i, j, k, fold, s, Ft, tau_lev, dt, hi);
            collide_cumulant(i, j, k, fold, s, Ft, tau_lev, dt, hi);
            // collide_cumulant_opt(i, j, k, fold, s, Ft, tau_lev, dt, hi);
            // collide_cumulant_opt2(i, j, k, fold, s, Ft, tau_lev, dt, hi);
        });
    }
}

void AmrCoreLBM::Stream(int lev, int n) {
    // amrex::AllPrint()<<"Stream on " << lev <<std::endl;

    int right = Geom(lev).Domain().length(0) - 1;
    int back = Geom(lev).Domain().length(1) - 1;
    int up = Geom(lev).Domain().length(2) - 1;
    amrex::IntVect hi{right, back, up};

    amrex::MultiFab& f_old_lev = f_old[lev];
    amrex::MultiFab& f_new_lev = f_new[lev];

    bool is_finest = {lev == finest_level};
    bool is_coarsest = {lev == 0};

    for (MFIter mfi(f_old_lev, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const auto bx = mfi.growntilebox(n);
        const Array4<Real>& fold = f_old_lev.array(mfi);
        const Array4<Real>& fnew = f_new_lev.array(mfi);

        amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) { stream(i, j, k, fold, fnew, hi, is_finest); });
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
        // particles[i]->InitChannelParticle(lev);
        // particles[i]->InitCylinderParticle(lev);
        //   particles[i] ->InitParticleFromFile(lev, "../../../object/sphere128");
        //   particles[i] ->InitParticleFromFile(lev, "../../../object/car_fine");
    }
}

void AmrCoreLBM::ComputeParticle(int lev) {
    // amrex::AllPrint()<<"ComputeParticle on " << lev <<std::endl;
    CommunicateLevel(lev);
    ComputeMacroLevel(lev);

    // 用户可选择以下两种方法之一（通过注释/取消注释切换）：
    // 方法1: IDF (Implicit Direct Forcing) - 矩阵求解法
    ApplyIDF(lev); // 使用 IDF 时保留此行，注释掉 InterpForce(lev)

    // 方法2: MDF (Multi-Direct Forcing) - 迭代求解法
    // InterpForce(lev); // 使用 MDF 时保留此行，注释掉 ApplyIDF(lev)
    // 注意：MDF 的迭代次数由 D3Q19.H 中的 NF 宏控制
    //       当 NF > 1 时自动启用两阶段迭代优化
}

void AmrCoreLBM::ReduceFxy(int lev, int step) {
    for (int i = 0; i < particle_num; i++) {
        particles[i]->SaveFxy(lev, step);
    }
}

void AmrCoreLBM::WriteForceDat(int lev, int step) {
    const amrex::MultiFab& force_dev = force[lev];
    const int ncomp = AMREX_SPACEDIM;

    // 将设备侧数据拷贝到主机侧 pinned 内存，便于 CPU 遍历输出
    amrex::MultiFab force_host(
        force_dev.boxArray(),
        force_dev.DistributionMap(),
        ncomp,
        0,
        amrex::MFInfo().SetArena(amrex::The_Pinned_Arena()));
    amrex::Copy(force_host, force_dev, 0, 0, ncomp, 0);

    const int myrank = amrex::ParallelDescriptor::MyProc();
    std::ostringstream oss;
    oss << "force_lev" << lev << "_step" << step << "_rank" << myrank << ".dat";
    std::ofstream ofs(oss.str());
    ofs.setf(std::ios::scientific);
    ofs.precision(8);
    ofs << "# i j k Fx Fy Fz\n";

    for (amrex::MFIter mfi(force_host); mfi.isValid(); ++mfi) {
        const amrex::Box& bx = mfi.validbox();
        const auto lo = bx.smallEnd();
        const auto hi = bx.bigEnd();
        const auto& Ft = force_host.const_array(mfi);
#if (AMREX_SPACEDIM == 3)
        for (int k = lo[2]; k <= hi[2]; ++k) {
            for (int j = lo[1]; j <= hi[1]; ++j) {
                for (int i = lo[0]; i <= hi[0]; ++i) {
                    amrex::Real fx = Ft(i, j, k, 0);
                    amrex::Real fy = Ft(i, j, k, 1);
                    amrex::Real fz = Ft(i, j, k, 2);
                    ofs << i << ' ' << j << ' ' << k << ' '
                        << fx << ' ' << fy << ' ' << fz << '\n';
                }
            }
        }
#else
        for (int j = lo[1]; j <= hi[1]; ++j) {
            for (int i = lo[0]; i <= hi[0]; ++i) {
                amrex::Real fx = Ft(i, j, 0, 0);
                amrex::Real fy = Ft(i, j, 0, 1);
                amrex::Real fz = ncomp > 2 ? Ft(i, j, 0, 2) : 0.0;
                ofs << i << ' ' << j << ' ' << 0 << ' '
                    << fx << ' ' << fy << ' ' << fz << '\n';
            }
        }
#endif
    }
    ofs.close();
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
        std::string filename = "data/dist.dat";
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
//                    IDF (Implicit Direct Forcing) 实现             //
//********************************************************************//

// 构建活跃欧拉点集合：遍历所有拉格朗日点，将其 5x5x5 邻域内落在几何域的欧拉网格点加入集合（去重）
// 同时收集全局拉格朗日点位置用于后续矩阵组装
void AmrCoreLBM::BuildActiveEulerSet(int lev) {
    if (particles.size() == 0 || !particles[0])
        return;

    // 静止边界优化：
    // 1. 矩阵 A^(-1) 只需构建一次（idf_geometry_built）
    // 2. 本地粒子计数 idf_local_NL 需要在每次 regrid 后更新（idf_local_count_valid）

    // 如果几何已构建且本地计数有效，直接返回
    if (idf_geometry_built && idf_local_count_valid) {
        return;
    }

    // 如果只需更新本地计数（几何已构建但 regrid 后计数过时）
    if (idf_geometry_built && !idf_local_count_valid) {
        // 由于粒子 redistribute 后，本进程拥有的粒子集合可能变化，仅更新本地计数
        idf_local_NL = 0;
        for (auto& pc : particles) {
            if (pc) {
                for (MyParIter pti(*pc, lev); pti.isValid(); ++pti) {
                    idf_local_NL += pti.numParticles();
                }
            }
        }
        idf_local_count_valid = true;
        return;
    }

    // ========== Step 1: 收集本地拉格朗日点数据和全局统计 ==========
    std::vector<Real> local_lag_pos;
    std::vector<int> local_lag_ids;
    idf_local_NL = 0;

    // 汇总所有粒子容器的拉格朗日点数据
    for (auto& pc : particles) {
        if (pc) {
            idf_local_NL += pc->IDF_CollectParticleData(lev, local_lag_pos, local_lag_ids);
        }
    }

    // MPI 汇总本地粒子数量到全局
    int nprocs = ParallelDescriptor::NProcs();
    idf_all_NL.resize(nprocs, 0);
    MPI_Allgather(&idf_local_NL, 1, MPI_INT, idf_all_NL.data(), 1, MPI_INT,
                  ParallelDescriptor::Communicator());

    // 计算全局粒子总数和本进程偏移
    idf_NL_global = 0;
    idf_local_offset = 0;
    for (int i = 0; i < nprocs; ++i) {
        if (i < ParallelDescriptor::MyProc()) {
            idf_local_offset += idf_all_NL[i];
        }
        idf_NL_global += idf_all_NL[i];
    }

    if (idf_NL_global == 0) {
        return;
    }

    // ========== Step 2: 汇总拉格朗日点位置到全局向量 ==========
    std::vector<int> pos_recvcounts(nprocs), pos_displs(nprocs + 1, 0);
    for (int i = 0; i < nprocs; ++i) {
        pos_recvcounts[i] = idf_all_NL[i] * 3; // 3D: x,y,z
    }
    std::partial_sum(pos_recvcounts.begin(), pos_recvcounts.end(), pos_displs.begin() + 1);

    std::vector<Real> unsorted_lag_pos(idf_NL_global * 3);
    MPI_Allgatherv(local_lag_pos.data(), idf_local_NL * 3, ParallelDescriptor::Mpi_typemap<Real>::type(),
                   unsorted_lag_pos.data(), pos_recvcounts.data(), pos_displs.data(),
                   ParallelDescriptor::Mpi_typemap<Real>::type(),
                   ParallelDescriptor::Communicator());

    // ========== Step 2c: 按粒子ID直接重排位置数据（无需排序）=====
    idf_lag_pos_global.resize(idf_NL_global * 3);
    std::vector<int> all_lag_ids(idf_NL_global);

    // Allgatherv 汇总所有粒子 ID 到每个进程
    std::vector<int> id_recvcounts(nprocs), id_displs(nprocs + 1, 0);
    for (int i = 0; i < nprocs; ++i) {
        id_recvcounts[i] = idf_all_NL[i];
    }
    std::partial_sum(id_recvcounts.begin(), id_recvcounts.end(), id_displs.begin() + 1);

    MPI_Allgatherv(local_lag_ids.data(), idf_local_NL, MPI_INT,
                   all_lag_ids.data(), id_recvcounts.data(), id_displs.data(),
                   MPI_INT, ParallelDescriptor::Communicator());

    // 按粒子 ID 直接写入全局位置数组
    for (int i = 0; i < idf_NL_global; ++i) {
        int pid = all_lag_ids[i];
        int global_idx = pid - 1; // 粒子ID从1开始
        if (global_idx >= 0 && global_idx < idf_NL_global) {
            idf_lag_pos_global[global_idx * 3] = unsorted_lag_pos[i * 3];
            idf_lag_pos_global[global_idx * 3 + 1] = unsorted_lag_pos[i * 3 + 1];
            idf_lag_pos_global[global_idx * 3 + 2] = unsorted_lag_pos[i * 3 + 2];
        }
    }

    // ========== Step 3: MPI 汇总欧拉点（各进程先独立计算，再合并去重）==========
    std::set<IntVect> local_euler_set;
    const Geometry& gm = Geom(lev);
    const Box& domain = gm.Domain();
    const Real* dx = gm.CellSize();

    // 构建本进程的局部欧拉点集合（所有拉格朗日点的 5x5x5 邻域）
    for (int p = 0; p < idf_local_NL; ++p) {
        Real xp = local_lag_pos[p * 3];
        Real yp = local_lag_pos[p * 3 + 1];
        Real zp = local_lag_pos[p * 3 + 2];
        Real xt = xp / dx[0];
        Real yt = yp / dx[1];
        Real zt = zp / dx[2];
        int xx = static_cast<int>(amrex::Math::floor(xt));
        int yy = static_cast<int>(amrex::Math::floor(yt));
        int zz = static_cast<int>(amrex::Math::floor(zt));

        for (int z = -2; z <= 2; ++z) {
            for (int y = -2; y <= 2; ++y) {
                for (int x = -2; x <= 2; ++x) {
                    IntVect iv(xx + x, yy + y, zz + z);
                    if (domain.contains(iv)) {
                        local_euler_set.insert(iv);
                    }
                }
            }
        }
    }

    // 将本地欧拉点转为数组 [i0, j0, k0, i1, j1, k1, ...]
    std::vector<int> local_euler_data;
    for (const auto& iv : local_euler_set) {
        local_euler_data.push_back(iv[0]);
        local_euler_data.push_back(iv[1]);
        local_euler_data.push_back(iv[2]);
    }
    int local_NE = local_euler_set.size();

    // 收集每个进程的欧拉点数量
    std::vector<int> all_NE(nprocs, 0);
    MPI_Allgather(&local_NE, 1, MPI_INT, all_NE.data(), 1, MPI_INT,
                  ParallelDescriptor::Communicator());

    // 准备 Allgatherv 参数
    int total_euler_ints = 0;
    std::vector<int> euler_recvcounts(nprocs), euler_displs(nprocs);
    for (int i = 0; i < nprocs; ++i) {
        euler_recvcounts[i] = all_NE[i] * 3; // 3D: i,j,k
        euler_displs[i] = total_euler_ints;
        total_euler_ints += euler_recvcounts[i];
    }

    // 汇总所有欧拉点数据
    std::vector<int> all_euler_data(total_euler_ints);
    MPI_Allgatherv(local_euler_data.data(), local_NE * 3, MPI_INT,
                   all_euler_data.data(), euler_recvcounts.data(), euler_displs.data(),
                   MPI_INT, ParallelDescriptor::Communicator());

    // 合并去重构建全局欧拉点集合
    std::set<IntVect> global_euler_set;
    for (int k = 0; k < total_euler_ints / 3; ++k) {
        IntVect iv(all_euler_data[k * 3], all_euler_data[k * 3 + 1], all_euler_data[k * 3 + 2]);
        global_euler_set.insert(iv);
    }

    // 更新全局欧拉点数据（每个进程都有完整副本）
    idf_active_euler_nodes_global.assign(global_euler_set.begin(), global_euler_set.end());
    idf_NE_global = global_euler_set.size();

    // 构建全局欧拉点索引映射
    idf_euler_index_map_global.clear();
    for (int i = 0; i < idf_NE_global; ++i) {
        idf_euler_index_map_global[idf_active_euler_nodes_global[i]] = i;
    }

    // 仅在第一次调用时输出详细信息
    static bool first_call = true;
    if (first_call && ParallelDescriptor::IOProcessor()) {
        amrex::Print() << "[IDF_3D] BuildActiveEulerSet: NL_global=" << idf_NL_global
                       << ", NE_global=" << idf_NE_global << std::endl;
        first_call = false;
    }

    // 标记几何已构建（静止边界可复用）
    idf_geometry_built = true;
    idf_local_count_valid = true;
}

//============================================================================//
//                     MDF (Multi-Direct Forcing) 实现                        //
//============================================================================//
// MDF 迭代求解：通过多次迭代收敛到目标速度
// 当 NF > 1 时，使用两阶段迭代避免 ghost 区域重复累加
void AmrCoreLBM::InterpForce(int lev) {
    amrex::MultiFab& rho_lev = density[lev];
    amrex::MultiFab& u_lev = velocity[lev];
    amrex::MultiFab& force_lev = force[lev];

    // 初始清零欧拉力场
    force_lev.setVal(0.0, nghost);

#if USE_MDF_TWO_STAGE
    // 调试：统计 InterpForce 被调用的次数
    // static int call_count = 0;
    // call_count++;

    // bool do_debug = (call_count >= 3120 && call_count <= 3160); // 只调试第3121到3160次调用

    // if (do_debug && amrex::ParallelDescriptor::IOProcessor()) {
    //     amrex::Print() << "[DEBUG] InterpForce call #" << call_count << " (n_iter=" << NF << ")" << std::endl;
    // }

    // MDF 两阶段迭代（NF > 1）：使用 force_delta 避免 ghost 区域重复累加
    // MDF 两阶段迭代（NF > 1）：动态创建临时 force_delta 以节省常驻内存
    // 临时变量在此作用域结束时自动释放，无需成员变量永久保存
    const amrex::BoxArray& ba = force_lev.boxArray();
    const amrex::DistributionMapping& dm = force_lev.DistributionMap();
    amrex::MultiFab force_delta_lev(ba, dm, AMREX_SPACEDIM, nghost);

    for (int i = 0; i < particle_num; i++) {
        particles[i]->ZeroParticleForce(lev);
    }

    for (int iter = 0; iter < NF; iter++) {
        // 1*. 清零力增量（包括 ghost 区域）
        force_delta_lev.setVal(0.0, nghost);

        // 2. 计算本次迭代的力增量 → 写入 force_delta
        for (int i = 0; i < particle_num; i++) {
            particles[i]->InterpForce(lev, rho_lev, u_lev, force_lev, force_delta_lev);
        }

        // 3. 通信：ghost → valid（SumBoundary）
        force_delta_lev.SumBoundary(geom[lev].periodicity());

        // 4*. 累加 force_delta 到 force（只需要 valid 区域）
        amrex::MultiFab::Add(force_lev, force_delta_lev, 0, 0, AMREX_SPACEDIM, 0);

        // 5*. 通信：valid → ghost（FillBoundary）
        force_lev.FillBoundary(geom[lev].periodicity());

        // 调试：输出粒子力
        // if (do_debug) {
        // for (int i = 0; i < particle_num; i++) {
        //     particles[i]->DebugPrintForceSum(lev, call_count, iter);
        // }
        // }
    }
#else
    // 单次迭代（NF = 1）：直接使用 force，无需 force_delta
    for (int i = 0; i < particle_num; i++) {
        particles[i]->InterpForce(lev, rho_lev, u_lev, force_lev);
        // particles[i]->InterpForceWallModel(lev, rho_lev, u_lev, force_lev);
    }
    SumForce(lev);

#endif // USE_MDF_TWO_STAGE
}

// MDF 辅助函数：对 force 场进行 SumBoundary 通信（ghost → valid）
void AmrCoreLBM::SumForce(int lev) {
    MultiFab* mf_pointer = &force[lev];
    mf_pointer->SumBoundary(Geom(lev).periodicity());
}

//============================================================================//
//                     IDF (Implicit Direct Forcing) 实现                     //
//============================================================================//
// 主入口：按顺序执行 IDF 各阶段
void AmrCoreLBM::ApplyIDF(int lev) {
    // 仅在最细层执行
    if (lev != finest_level) {
        return;
    }

    // Step 1: 插值欧拉速度到拉格朗日点（包括全局汇总）
    IDF_InterpolateEulerToLag(lev);

    // Step 2: 组装全局矩阵 A = D_I × D_E
    IDF_AssembleMatrix(lev);

    // Step 3: 求解线性系统得到拉格朗日力
    IDF_SolveSystem(lev);

    // Step 4: 传播拉格朗日力到欧拉网格
    IDF_SpreadLagToEuler(lev);

    SumForce(lev);
}

// 插值欧拉速度到拉格朗日点，收集到全局向量
void AmrCoreLBM::IDF_InterpolateEulerToLag(int lev) {
    if (particles.size() == 0 || !particles[0])
        return;

    // Step 1: 构建活跃欧拉点集合和全局拉格朗日点位置
    BuildActiveEulerSet(lev);

    if (idf_NL_global == 0) {
        if (ParallelDescriptor::IOProcessor()) {
            amrex::Print() << "[IDF_3D] Warning: No Lagrangian points found!" << std::endl;
        }
        return;
    }

    // Step 2: 使用 LagrangeParticleContainer 的接口执行插值
    amrex::MultiFab& u_lev = velocity[lev];
    amrex::MultiFab& rho_lev = density[lev];

    for (auto& pc : particles) {
        if (pc) {
            pc->IDF_Interpolate(lev, u_lev, rho_lev);
        }
    }

    // Step 2b: 从粒子属性中读取插值结果到 CPU vector
    std::vector<Real> local_interp_ux, local_interp_uy, local_interp_uz, local_interp_rho;
    std::vector<int> local_interp_ids;

    for (auto& pc : particles) {
        if (pc) {
            std::vector<Real> temp_ux, temp_uy, temp_uz, temp_rho;
            std::vector<int> temp_ids;
            pc->IDF_ReadInterpResults(lev, temp_ux, temp_uy, temp_uz, temp_rho, &temp_ids);
            local_interp_ux.insert(local_interp_ux.end(), temp_ux.begin(), temp_ux.end());
            local_interp_uy.insert(local_interp_uy.end(), temp_uy.begin(), temp_uy.end());
            local_interp_uz.insert(local_interp_uz.end(), temp_uz.begin(), temp_uz.end());
            local_interp_rho.insert(local_interp_rho.end(), temp_rho.begin(), temp_rho.end());
            local_interp_ids.insert(local_interp_ids.end(), temp_ids.begin(), temp_ids.end());
        }
    }

    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(static_cast<int>(local_interp_ux.size()) == idf_local_NL,
                                     "IDF_3D local buffer size mismatch");

    // Step 3: 汇总插值速度到全局向量（使用 Allgatherv 一步完成广播）
    int nprocs = ParallelDescriptor::NProcs();
    std::vector<int> interp_displs(nprocs + 1, 0);
    std::partial_sum(idf_all_NL.begin(), idf_all_NL.end(), interp_displs.begin() + 1);

    // Step 3a: 汇总插值数据（ux, uy, uz, rho, ids）
    std::vector<Real> unsorted_interp_ux(idf_NL_global), unsorted_interp_uy(idf_NL_global),
        unsorted_interp_uz(idf_NL_global), unsorted_interp_rho(idf_NL_global);
    std::vector<int> all_interp_ids(idf_NL_global);

    MPI_Allgatherv(local_interp_ux.data(), idf_local_NL, ParallelDescriptor::Mpi_typemap<Real>::type(),
                   unsorted_interp_ux.data(), idf_all_NL.data(), interp_displs.data(),
                   ParallelDescriptor::Mpi_typemap<Real>::type(), ParallelDescriptor::Communicator());

    MPI_Allgatherv(local_interp_uy.data(), idf_local_NL, ParallelDescriptor::Mpi_typemap<Real>::type(),
                   unsorted_interp_uy.data(), idf_all_NL.data(), interp_displs.data(),
                   ParallelDescriptor::Mpi_typemap<Real>::type(), ParallelDescriptor::Communicator());

    MPI_Allgatherv(local_interp_uz.data(), idf_local_NL, ParallelDescriptor::Mpi_typemap<Real>::type(),
                   unsorted_interp_uz.data(), idf_all_NL.data(), interp_displs.data(),
                   ParallelDescriptor::Mpi_typemap<Real>::type(), ParallelDescriptor::Communicator());

    MPI_Allgatherv(local_interp_rho.data(), idf_local_NL, ParallelDescriptor::Mpi_typemap<Real>::type(),
                   unsorted_interp_rho.data(), idf_all_NL.data(), interp_displs.data(),
                   ParallelDescriptor::Mpi_typemap<Real>::type(), ParallelDescriptor::Communicator());

    MPI_Allgatherv(local_interp_ids.data(), idf_local_NL, MPI_INT,
                   all_interp_ids.data(), idf_all_NL.data(), interp_displs.data(),
                   MPI_INT, ParallelDescriptor::Communicator());

    // Step 3b: 按粒子ID直接写入全局数组
    idf_interp_u_x.resize(idf_NL_global);
    idf_interp_u_y.resize(idf_NL_global);
    idf_interp_u_z.resize(idf_NL_global);
    idf_interp_rho.resize(idf_NL_global);

    for (int i = 0; i < idf_NL_global; ++i) {
        int pid = all_interp_ids[i];
        int global_idx = pid - 1;
        if (global_idx >= 0 && global_idx < idf_NL_global) {
            idf_interp_u_x[global_idx] = unsorted_interp_ux[i];
            idf_interp_u_y[global_idx] = unsorted_interp_uy[i];
            idf_interp_u_z[global_idx] = unsorted_interp_uz[i];
            idf_interp_rho[global_idx] = unsorted_interp_rho[i];
        }
    }

    // Step 4: 初始化目标速度（静止边界，u_target = 0）
    idf_target_u_x.assign(idf_NL_global, 0.0);
    idf_target_u_y.assign(idf_NL_global, 0.0);
    idf_target_u_z.assign(idf_NL_global, 0.0);

    // 仅在第一次调用时输出
    static bool first_interp = true;
    if (first_interp && ParallelDescriptor::IOProcessor()) {
        amrex::Print() << "[IDF_3D] Interpolated Euler velocity to Lagrangian points: NL_global="
                       << idf_NL_global << ", NE_global=" << idf_NE_global << std::endl;
        first_interp = false;
    }
}

// 组装全局矩阵 A = D_I × D_E（每个进程独立计算完整矩阵）
void AmrCoreLBM::IDF_AssembleMatrix(int lev) {
    if (idf_NL_global == 0 || idf_NE_global == 0)
        return;

    // 如果矩阵已构建且大小正确，跳过
    if (idf_matrix_built &&
        idf_A.size() == static_cast<size_t>(idf_NL_global * idf_NL_global))
        return;

    const Geometry& gm = Geom(lev);
    const Real* dx = gm.CellSize();

    int NL = idf_NL_global;
    int NE = idf_NE_global;

    // 分配 D_I (NL × NE) 和 D_E (NE × NL) 作为稀疏表示
    std::vector<std::vector<std::pair<int, Real>>> DI_rows(NL);
    std::vector<std::vector<std::pair<int, Real>>> DE_cols(NL);

    auto const& emap = idf_euler_index_map_global;

    // 基于全局拉格朗日点位置构建 D_I 和 D_E
    for (int p = 0; p < NL; ++p) {
        Real xp = idf_lag_pos_global[3 * p];
        Real yp = idf_lag_pos_global[3 * p + 1];
        Real zp = idf_lag_pos_global[3 * p + 2];
        Real xt = xp / dx[0];
        Real yt = yp / dx[1];
        Real zt = zp / dx[2];
        int xx = static_cast<int>(amrex::Math::floor(xt));
        int yy = static_cast<int>(amrex::Math::floor(yt));
        int zz = static_cast<int>(amrex::Math::floor(zt));

        for (int z = -2; z <= 2; ++z) {
            for (int y = -2; y <= 2; ++y) {
                for (int x = -2; x <= 2; ++x) {
                    IntVect iv(xx + x, yy + y, zz + z);

                    auto it = emap.find(iv);
                    if (it == emap.end())
                        continue;

                    int eidx = it->second;
                    Real lx = xt - (iv[0] + 0.5);
                    Real ly = yt - (iv[1] + 0.5);
                    Real lz = zt - (iv[2] + 0.5);
                    Real wI = delta3p(lx) * delta3p(ly) * delta3p(lz);
                    Real wE = wI; // 暂时删除权重IB_weight, 后续计算再添加

                    if (std::abs(wI) > 1e-15) {
                        DI_rows[p].emplace_back(eidx, wI);
                    }
                    if (std::abs(wE) > 1e-15) {
                        DE_cols[p].emplace_back(eidx, wE);
                    }
                }
            }
        }
    }

    // 将 D_E 按欧拉索引分组
    std::vector<std::vector<std::pair<int, Real>>> DE_by_euler(NE);
    for (int j = 0; j < NL; ++j) {
        for (auto const& ew : DE_cols[j]) {
            int eidx = ew.first;
            Real wE = ew.second;
            if (eidx >= 0 && eidx < NE) {
                DE_by_euler[eidx].emplace_back(j, wE);
            }
        }
    }

    // 分配并构建 A = D_I × D_E (NL × NL)
    idf_A.assign(static_cast<size_t>(NL) * NL, 0.0);

    // A[i,j] = sum_e D_I[i,e] * D_E[e,j]
    for (int i = 0; i < NL; ++i) {
        for (auto const& ie : DI_rows[i]) {
            int eidx = ie.first;
            Real wI = ie.second;
            if (eidx < 0 || eidx >= NE)
                continue;
            for (auto const& jw : DE_by_euler[eidx]) {
                int j = jw.first;
                Real wE = jw.second;
                idf_A[i * NL + j] += wI * wE;
            }
        }
    }

    idf_matrix_built = true;

    // ======== 静止边界优化：预计算 A 的逆矩阵 ========
    if (!idf_inverse_built) {
        if (ParallelDescriptor::IOProcessor()) {
            amrex::Print() << "[IDF_3D] Computing A^(-1) for static boundary..." << std::endl;
        }

        bool success = computeMatrixInverse(idf_A, idf_A_inv, NL);

        if (success) {
            idf_inverse_built = true;
            if (ParallelDescriptor::IOProcessor()) {
                Real maxAinv = 0.0;
                for (int i = 0; i < NL * NL; ++i) {
                    maxAinv = std::max(maxAinv, std::abs(idf_A_inv[i]));
                }
                amrex::Print() << "[IDF_3D] A^(-1) computed successfully, max|A^(-1)|="
                               << maxAinv << std::endl;
            }
        }
    }
}

// 求解线性系统得到拉格朗日力
void AmrCoreLBM::IDF_SolveSystem(int lev) {
    int NL = idf_NL_global;
    if (NL == 0)
        return;

    // Step 1: 构建 RHS: b = u_interp - u_target（在3D中，密度是真实密度，不是压力）
    idf_rhs_x.resize(NL);
    idf_rhs_y.resize(NL);
    idf_rhs_z.resize(NL);

    for (int i = 0; i < NL; ++i) {
        idf_rhs_x[i] = idf_interp_u_x[i] - idf_target_u_x[i];
        idf_rhs_y[i] = idf_interp_u_y[i] - idf_target_u_y[i];
        idf_rhs_z[i] = idf_interp_u_z[i] - idf_target_u_z[i];
    }

    // Step 2: 求解线性系统 A * f_vel = rhs
    idf_sol_x.resize(NL);
    idf_sol_y.resize(NL);
    idf_sol_z.resize(NL);

    if (idf_inverse_built) {
        // 使用预计算的 A^(-1)：矩阵-向量乘法
        // 注意：3D模型中密度是真实密度，不是压力，所以不需要密度系数
        denseMatVec(idf_A_inv, idf_rhs_x, idf_sol_x);
        denseMatVec(idf_A_inv, idf_rhs_y, idf_sol_y);
        denseMatVec(idf_A_inv, idf_rhs_z, idf_sol_z);
    }

    // Step 3: 将力写回粒子属性
    for (auto& pc : particles) {
        if (pc) {
            pc->IDF_WriteForceToParticles(lev, idf_sol_x, idf_sol_y, idf_sol_z, idf_NL_global);
        }
    }
}

// 传播拉格朗日力到欧拉网格
void AmrCoreLBM::IDF_SpreadLagToEuler(int lev) {
    if (idf_NL_global == 0)
        return;

    amrex::MultiFab& force_lev = force[lev];
    force_lev.setVal(0.0, nghost);

    // 使用 LagrangeParticleContainer 接口传播力
    for (auto& pc : particles) {
        if (pc) {
            pc->IDF_SpreadForce(lev, force_lev);
        }
    }

    // 仅在第一次调用时输出
    static bool first_spread = true;
    if (first_spread && ParallelDescriptor::IOProcessor()) {
        amrex::Print() << "[IDF_3D] Spread Lagrangian forces to Euler grid done." << std::endl;
        first_spread = false;
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

    FillDdfPatch(lev, time, old_state);
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

    amrex::Real dx = Geom(lev).CellSizeArray()[0];
    int right = Geom(lev).Domain().length(0) - 1;
    int back = Geom(lev).Domain().length(1) - 1;
    int up = Geom(lev).Domain().length(2) - 1;
    amrex::IntVect hi{right, back, up};

    for (MFIter mfi(f_old_lev, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const Box& bx = mfi.growntilebox(nghost);
        Array4<Real> const& fold = f_old_lev.array(mfi);
        Array4<Real> const& fnew = f_new_lev.array(mfi);

        amrex::ParallelForRNG(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k, RandomEngine const& rd) { init_fluid(i, j, k, fold, fnew); });
        // init_fluid_channel(i, j, k, fold, fnew, hi, dx, rd); });
    }
}

/*************************************第3版*ErrorEst***************************************/
void AmrCoreLBM::ErrorEst(int lev, amrex::TagBoxArray& tags, amrex::Real time, int ngrow) {
    // amrex::AllPrint()<<"ErrorEst on " << lev <<std::endl;

    if (lev >= err.size()) {
        return;
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
            // state_error_3(i, j, k, tagfab, vort, err_value, tagval, clearval, lev, geomdata, lo2, hi2, points_p, points_num);
            // state_error_4(i, j, k, tagfab, vort, err_value, tagval, clearval, lev, geomdata, lo2, hi2, points_p, points_num);
            state_error_5(i, j, k, tagfab, vort, err_value, tagval, clearval, lev, geomdata, lo2, hi2, points_p, points_num); });
    }
}

void AmrCoreLBM::WriteCheckpoint(int step, amrex::Real time) const {
    const std::string level_prefix = "Level_";
    const bool isIOP = ParallelDescriptor::IOProcessor();

    // 新策略：统一使用 step-suffixed 目录，以便可保留多个 checkpoint
    const std::string out_chkname = amrex::Concatenate(params_.chk_prefix, step, 8);
    const bool write_particles = params_.write_particles;

    if (isIOP) {
        amrex::Print() << "[Checkpoint] Writing '" << out_chkname
                       << "' step=" << step << " time=" << time << '\n';

        // 覆盖写：仅清理并重建目标目录
        // 目录可能已存在（重复写同一步），先清理再创建
        amrex::UtilCreateCleanDirectory(out_chkname, false); // 这里必须是false, 否则会造成不同步, 进而报错

        // 预先创建 Level_* 目录
        for (int lev = 0; lev <= finest_level; ++lev) {
            const std::string level_dir = out_chkname + "/" + level_prefix + std::to_string(lev);
            amrex::UtilCreateDirectory(level_dir, 0755);
        }
        // 如需写粒子，预创建粒子容器目录
        if (write_particles) {
            for (int i = 0; i < particle_num; ++i) {
                const std::string pdir = out_chkname + "/particles_" + std::to_string(i);
                amrex::UtilCreateDirectory(pdir, 0755);
            }
        }
    }
    ParallelDescriptor::Barrier(); // 确保所有 rank 在进入写阶段前看到一致的目录状态

    // 写 Header
    if (isIOP) {
        amrex::Print() << "[Checkpoint] Writing header for '" << out_chkname
                       << "' levels=0.." << finest_level << '\n';

        std::ofstream header(out_chkname + "/Header", std::ios::out | std::ios::trunc);
        if (!header.is_open()) {
            amrex::Print() << "[Checkpoint][ERROR] Cannot open '" << out_chkname
                           << "/Header' for writing. Abort checkpoint write for this step.\n";
            return; // 提前返回避免后续写入目录缺少头文件导致重启失败
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

    // 写多重网格数据
    for (int lev = 0; lev <= finest_level; ++lev) {
        const auto& f_old_mf = f_old[lev];
        const auto& f_new_mf = f_new[lev];

        VisMF::Write(f_old_mf, MultiFabFileFullPrefix(lev, out_chkname, level_prefix, "f_old"));
        VisMF::Write(f_new_mf, MultiFabFileFullPrefix(lev, out_chkname, level_prefix, "f_new"));
    }

    // 粒子可选输出
    if (!write_particles) {
        if (isIOP) {
            amrex::Print() << "[Checkpoint] Skip particle containers (checkpoint.write_particles=0)\n";
        }
    } else {
        // 写粒子数据（所有 rank 参与，IOProc 已创建目录）
        for (int i = 0; i < particle_num; ++i) {
            if (particles[i]) {
                const std::string pname = "particles_" + std::to_string(i);
                particles[i]->Checkpoint(out_chkname, pname);
            }
        }
    }
    ParallelDescriptor::Barrier(); // 无论是否写粒子，都在清理前同步

    // 根据 keep_latest_only 决定是否删除旧的 checkpoint 目录（仅 IOProc）
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
                return std::all_of(name.begin() + params_.chk_prefix.size(), name.end(), [](unsigned char ch) {
                    return std::isdigit(ch);
                });
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
    // 读取各层 BoxArray 与 DistributionMapping
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

    // 方法1：按 checkpoint 重建 AMR 层级（允许网格变化）。
    // 更新 finest_level（AmrCore 的内部状态）与每层的 BA/DM。
    SetFinestLevel(finest_in_file);
    for (int lev = 0; lev <= finest_level; ++lev) {
        SetBoxArray(lev, ba_file[lev]);
        SetDistributionMap(lev, dm_file[lev]);
    }

    const std::string level_prefix = "Level_";

    // 重新定义并读取多重网格数据
    for (int lev = 0; lev <= finest_level; ++lev) {
        // 先清掉旧结构
        ClearLevel(lev);
        // 用 checkpoint 中的 BA/DM 重新定义
        f_old[lev].define(boxArray(lev), DistributionMap(lev), Q, nghost);
        f_new[lev].define(boxArray(lev), DistributionMap(lev), Q, nghost);
        velocity[lev].define(boxArray(lev), DistributionMap(lev), AMREX_SPACEDIM, nghost);
        density[lev].define(boxArray(lev), DistributionMap(lev), 1, nghost);
        vorticity[lev].define(boxArray(lev), DistributionMap(lev), 2, nghost);
        shear[lev].define(boxArray(lev), DistributionMap(lev), 1, nghost);
        force[lev].define(boxArray(lev), DistributionMap(lev), AMREX_SPACEDIM, nghost);

        VisMF::Read(f_old[lev], MultiFabFileFullPrefix(lev, chkname, level_prefix, "f_old"));
        VisMF::Read(f_new[lev], MultiFabFileFullPrefix(lev, chkname, level_prefix, "f_new"));
        // velocity 与 density 不再从磁盘读取，将在读完后通过 ComputeMacro() 重建

        // 这些字段通常可由 f_old/f_new 或速度场推导，这里初始化为 0，避免未定义值
        velocity[lev].setVal(0.0, nghost);
        density[lev].setVal(0.0, nghost);
        vorticity[lev].setVal(0.0, nghost);
        shear[lev].setVal(0.0, nghost);
        force[lev].setVal(0.0, nghost);
    }

    // 读取粒子容器：若容器尚未构造（例如从 checkpoint 重启且跳过 InitParticle），
    // 则先构造容器对象，再从磁盘恢复粒子数据。
    for (int i = 0; i < particle_num; ++i) {
        const std::string pname = "particles_" + std::to_string(i);
        // 为了与新的 BA/DM 对齐，重建容器再恢复
        particles[i].reset();
        particles[i] = std::make_unique<LagrangeParticleContainer>(this, points[i], i);
        particles[i]->Restart(chkname, pname);
    }
}
