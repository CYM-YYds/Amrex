#include <AMReX_ParallelDescriptor.H>
#include <AMReX_ParmParse.H>
#include <AMReX_MultiFabUtil.H>
#include <AMReX_PlotFileUtil.H>
#include <AMReX_VisMF.H>
#include <AMReX_PhysBCFunct.H>
#include <AMReX_MFIter.H>
#include <AMReX_ParIter.H>
#include <AMReX_GpuContainers.H> // 用于 Gpu::PinnedVector
#include <AMReX_Utility.H>

#include <mpi.h> // 显式包含 MPI 用于 Allgatherv
#include <dirent.h>
#include <cctype>
#include <cstdlib>
#include <set>
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
// #include <unordered_map>
#include <algorithm> // 用于 std::sort, std::min_element
#include <numeric>   // 用于 std::iota

#ifdef AMREX_MEM_PROFILING
#include <AMReX_MemProfiler.H>
#endif

#include "AmrCoreLBM.H"
#include "Kernels.H"

// IDF: 线性求解器 - BiCGSTAB（双共轭梯度稳定法，适用于非对称矩阵）
namespace {
// 稠密矩阵乘向量 A*x (A 按行主序存储为 N*N 的一维数组)
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

// 稠密矩阵乘向量并应用缩放系数: y[i] = scale[i] * (A*x)[i]
// 用于 IDF 中合并矩阵乘法和力密度计算，避免额外循环
void denseMatVecWithScale(const std::vector<amrex::Real>& A,
                          const std::vector<amrex::Real>& x,
                          const std::vector<amrex::Real>& idf_interp_rho,
                          std::vector<amrex::Real>& y) {
    int N = static_cast<int>(x.size());
    y.assign(N, 0.0);
    for (int i = 0; i < N; ++i) {
        const amrex::Real* row = &A[i * N];
        amrex::Real acc = 0.0;
        for (int j = 0; j < N; ++j)
            acc += row[j] * x[j];
        y[i] = 2.0 * idf_interp_rho[i] / cs2 / dt_min * acc; // 这里除以 cs2是模型的特殊性
    }
}

// 向量点积
amrex::Real dotProduct(const std::vector<amrex::Real>& a,
                       const std::vector<amrex::Real>& b) {
    amrex::Real sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        sum += a[i] * b[i];
    return sum;
}

// BiCGSTAB 求解器：求解 A x = b（适用于非对称矩阵）
// A: N*N 稠密矩阵（行主序）, b: 右端向量, x: 解向量（输出）
bool bicgstabSolve(const std::vector<amrex::Real>& A,
                   const std::vector<amrex::Real>& b,
                   std::vector<amrex::Real>& x,
                   int max_iter = 2000, amrex::Real tol = 1e-10) {
    int N = static_cast<int>(b.size());
    if (N == 0)
        return true;

    x.assign(N, 0.0);

    // r = b - A*x = b (因为 x=0)
    std::vector<amrex::Real> r(b);
    std::vector<amrex::Real> r_hat(r); // 选择 r_hat = r_0

    amrex::Real rho_old = 1.0, alpha = 1.0, omega = 1.0;
    std::vector<amrex::Real> v(N, 0.0), p(N, 0.0), s(N), t(N);

    amrex::Real b_norm = std::sqrt(dotProduct(b, b));
    if (b_norm < 1e-20)
        b_norm = 1.0; // 避免除零

    for (int iter = 0; iter < max_iter; ++iter) {
        amrex::Real rho_new = dotProduct(r_hat, r);

        // 检查 rho 是否接近零（breakdown）
        if (std::abs(rho_new) < 1e-30) {
            if (amrex::ParallelDescriptor::IOProcessor()) {
                amrex::Print() << "[BiCGSTAB] Breakdown at iter " << iter
                               << ", rho=" << rho_new << std::endl;
            }
            return false;
        }

        amrex::Real beta = (rho_new / rho_old) * (alpha / omega);

        // p = r + beta * (p - omega * v)
        for (int i = 0; i < N; ++i) {
            p[i] = r[i] + beta * (p[i] - omega * v[i]);
        }

        // v = A * p
        denseMatVec(A, p, v);

        amrex::Real r_hat_v = dotProduct(r_hat, v);
        if (std::abs(r_hat_v) < 1e-30) {
            if (amrex::ParallelDescriptor::IOProcessor()) {
                amrex::Print() << "[BiCGSTAB] Breakdown (r_hat·v=0) at iter " << iter << std::endl;
            }
            return false;
        }

        alpha = rho_new / r_hat_v;

        // s = r - alpha * v
        for (int i = 0; i < N; ++i) {
            s[i] = r[i] - alpha * v[i];
        }

        // 检查 s 是否已经足够小
        amrex::Real s_norm = std::sqrt(dotProduct(s, s));
        if (s_norm / b_norm < tol) {
            // x = x + alpha * p
            for (int i = 0; i < N; ++i) {
                x[i] += alpha * p[i];
            }
            if (amrex::ParallelDescriptor::IOProcessor()) {
                amrex::Print() << "[BiCGSTAB] Converged at iter " << iter + 1
                               << ", residual=" << s_norm / b_norm << std::endl;
            }
            return true;
        }

        // t = A * s
        denseMatVec(A, s, t);

        amrex::Real t_dot_t = dotProduct(t, t);
        amrex::Real t_dot_s = dotProduct(t, s);

        if (std::abs(t_dot_t) < 1e-30) {
            omega = 0.0;
        } else {
            omega = t_dot_s / t_dot_t;
        }

        // x = x + alpha * p + omega * s
        for (int i = 0; i < N; ++i) {
            x[i] += alpha * p[i] + omega * s[i];
        }

        // r = s - omega * t
        for (int i = 0; i < N; ++i) {
            r[i] = s[i] - omega * t[i];
        }

        // 检查收敛
        amrex::Real r_norm = std::sqrt(dotProduct(r, r));
        if (r_norm / b_norm < tol) {
            if (amrex::ParallelDescriptor::IOProcessor()) {
                amrex::Print() << "[BiCGSTAB] Converged at iter " << iter + 1
                               << ", residual=" << r_norm / b_norm << std::endl;
            }
            return true;
        }

        // 每 1000 步输出进度
        if ((iter + 1) % 1000 == 0 && amrex::ParallelDescriptor::IOProcessor()) {
            amrex::Print() << "[BiCGSTAB] iter=" << iter + 1
                           << ", residual=" << r_norm / b_norm << std::endl;
        }

        if (std::abs(omega) < 1e-30) {
            if (amrex::ParallelDescriptor::IOProcessor()) {
                amrex::Print() << "[BiCGSTAB] Breakdown (omega=0) at iter " << iter << std::endl;
            }
            return false;
        }

        rho_old = rho_new;
    }

    if (amrex::ParallelDescriptor::IOProcessor()) {
        amrex::Real r_norm = std::sqrt(dotProduct(r, r));
        amrex::Print() << "[BiCGSTAB] Warning: not converged after " << max_iter
                       << " iters, residual=" << r_norm / b_norm << std::endl;
    }
    return false;
}

// 保留 Jacobi 作为备用（调试用）
void jacobiSolve(const std::vector<amrex::Real>& A,
                 const std::vector<amrex::Real>& b,
                 std::vector<amrex::Real>& x,
                 int max_iter = 200, amrex::Real tol = 1e-10) {
    int N = static_cast<int>(b.size());
    x.assign(N, 0.0);
    std::vector<amrex::Real> xnew(N, 0.0);
    for (int it = 0; it < max_iter; ++it) {
        for (int i = 0; i < N; ++i) {
            amrex::Real diag = A[i * N + i];
            if (std::abs(diag) < 1e-14)
                continue;
            amrex::Real sigma = 0.0;
            for (int j = 0; j < N; ++j)
                if (j != i)
                    sigma += A[i * N + j] * x[j];
            xnew[i] = (b[i] - sigma) / diag;
        }
        amrex::Real maxdiff = 0.0;
        for (int i = 0; i < N; ++i) {
            maxdiff = std::max(maxdiff, std::abs(xnew[i] - x[i]));
            x[i] = xnew[i];
        }
        if (maxdiff < tol)
            break;
    }
}

// LU 分解求矩阵逆（带部分主元选择）
// A: N×N 输入矩阵（行主序），A_inv: N×N 输出逆矩阵
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
        aug[i * 2 * N + N + i] = 1.0; // 单位矩阵
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
    // force_delta 现在仅作为临时局部变量在 InterpForce() 中创建，无需预先 resize

    // IDF: 仅最细层使用的活跃欧拉点集合与计数
    idf_active_euler_nodes.clear();
    idf_NE = 0;
    idf_euler_index_map.clear();

    tau.resize(nlevs_max);
    tau[0] = tau_0;

    for (int lev = 1; lev <= max_level; ++lev) {
        tau[lev] = 2 * (tau[lev - 1] - 0.5) + 0.5;
    }

    int bc_lo[] = {BCType::foextrap, BCType::foextrap, BCType::foextrap};
    int bc_hi[] = {BCType::foextrap, BCType::foextrap, BCType::foextrap};

    bcs.resize(Q);

    for (int idim = 0; idim < DIM; idim++) {
        for (int comp = 0; comp < Q; ++comp) {
            bcs[comp].setLo(idim, bc_lo[idim]);
            bcs[comp].setHi(idim, bc_hi[idim]);
        }
    }

    static_lo.resize(2 * nlevs_max);
    static_hi.resize(2 * nlevs_max);

    for (int lev = 0; lev <= max_level; lev++) {
        amrex::Real dx = Geom(lev).CellSizeArray()[0];

        for (int idim = 0; idim < AMREX_SPACEDIM; idim++) {
            static_lo[lev][idim] = 0 + (16); // 边界加密
            static_hi[lev][idim] = Geom(lev).Domain().length(idim) - (16);
        }
        if (lev == 0) {
            static_lo[lev + nlevs_max][0] = (center[0] - 5.0 * D) * dx_0 / dx;
            static_lo[lev + nlevs_max][1] = (center[1] - 5.0 * D) * dx_0 / dx;

            static_hi[lev + nlevs_max][0] = (center[0] + 22.0 * D) * dx_0 / dx;
            static_hi[lev + nlevs_max][1] = (center[1] + 5.0 * D) * dx_0 / dx;
        }
        if (lev == 1) {
            static_lo[lev + nlevs_max][0] = (center[0] - 3.0 * D) * dx_0 / dx;
            static_lo[lev + nlevs_max][1] = (center[1] - 3.0 * D) * dx_0 / dx;

            static_hi[lev + nlevs_max][0] = (center[0] + 20.0 * D) * dx_0 / dx;
            static_hi[lev + nlevs_max][1] = (center[1] + 3.0 * D) * dx_0 / dx;
        }
        if (lev == 2) {
            static_lo[lev + nlevs_max][0] = (center[0] - 2.0 * D) * dx_0 / dx;
            static_lo[lev + nlevs_max][1] = (center[1] - 1.5 * D) * dx_0 / dx;

            static_hi[lev + nlevs_max][0] = (center[0] + 20.0 * D) * dx_0 / dx;
            static_hi[lev + nlevs_max][1] = (center[1] + 1.5 * D) * dx_0 / dx;
        }
    }
}

AmrCoreLBM::AmrCoreLBM() {
}
AmrCoreLBM::~AmrCoreLBM() = default;

void AmrCoreLBM::ResetIDFCheckpointState() {
    idf_interp_u_x.clear();
    idf_interp_u_y.clear();
    idf_interp_rho.clear();
    idf_target_u_x.clear();
    idf_target_u_y.clear();
    idf_rhs_x.clear();
    idf_rhs_y.clear();
    idf_sol_x.clear();
    idf_sol_y.clear();
    idf_A.clear();
    idf_A_inv.clear();
    idf_lag_pos_global.clear();
    idf_all_NL.clear();
    idf_active_euler_nodes.clear();
    idf_euler_index_map.clear();
    idf_active_euler_nodes_global.clear();
    idf_euler_index_map_global.clear();

    idf_inverse_built = false;
    idf_matrix_built = false;
    idf_geometry_built = false;
    idf_local_count_valid = false;

    idf_NL_global = 0;
    idf_local_offset = 0;
    idf_local_NL = 0;
    idf_NE = 0;
    idf_NE_global = 0;
}

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
void AmrCoreLBM::PrintLbmParm() {
    amrex::Print() << "╔════════════════════════════════════════════════════════╗" << std::endl;
    amrex::Print() << "║               LBM Parameters                           ║" << std::endl;
    amrex::Print() << "╚════════════════════════════════════════════════════════╝" << std::endl;

    amrex::Print() << std::setw(15) << std::left << "  NX     =" << std::setw(10) << std::right << Geom(0).Domain().length(0) << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  NY     =" << std::setw(10) << std::right << Geom(0).Domain().length(1) << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  dx_0   =" << std::setw(10) << std::right << dx_0 << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  dx_min =" << std::setw(10) << std::right << dx_min << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  Re     =" << std::setw(10) << std::right << Re << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  cs2    =" << std::setw(10) << std::right << cs2 << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  p0     =" << std::setw(10) << std::right << p0 << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  Ma     =" << std::setw(10) << std::right << Ma << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  U0     =" << std::setw(10) << std::right << U0 << std::endl;

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

    {
        ParmParse pp_amr("amr");
        pp_amr.query("regrid_int", params_.regrid_int);
        pp_amr.query("plot_int", params_.plot_int);
        pp_amr.query("begin_int", params_.begin_int);
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
                           << "  begin_int        = " << params_.begin_int << "\n"
                           << "  regrid_int       = " << params_.regrid_int << "\n";
        }
    }
}

void AmrCoreLBM::WriteVelocityFile(const int step, const amrex::Real time) {
    const std::string& plotfilename = amrex::Concatenate(plot_file, step, 5);

    amrex::Vector<const amrex::MultiFab*> mf;

    for (int i = 0; i <= finest_level; ++i) {
        mf.push_back(&velocity[i]);
    }

    amrex::Vector<std::string> varnames = {"ux", "uy"};

    amrex::WriteMultiLevelPlotfile(plotfilename, finest_level + 1, mf, varnames,
                                   Geom(), time, Vector<int>(finest_level + 1, step), refRatio());
}

void AmrCoreLBM::WriteVorticityFile(const int step, const amrex::Real time) {
    std::string plot_file_vort{plot_file + "vort_"};
    const std::string& plotfilename = amrex::Concatenate(plot_file_vort, step, 5);

    amrex::Vector<const amrex::MultiFab*> mf;

    for (int i = 0; i <= finest_level; ++i) {
        mf.push_back(&vorticity[i]);
    }

    amrex::Vector<std::string> varnames = {"Vorticity"};

    amrex::WriteMultiLevelPlotfile(plotfilename, finest_level + 1, mf, varnames,
                                   Geom(), time, Vector<int>(finest_level + 1, step), refRatio());
}

void AmrCoreLBM::WriteParticleFile(const int step, const amrex::Real time) {
    mypc->WriteParticle(step);
}

void AmrCoreLBM::WriteCheckpoint(int step, amrex::Real time) const {
    const std::string level_prefix = "Level_";
    const bool is_iop = ParallelDescriptor::IOProcessor();
    const std::string out_chkname = amrex::Concatenate(params_.chk_prefix, step, 8);

    if (is_iop) {
        amrex::Print() << "[Checkpoint] Writing '" << out_chkname
                       << "' step=" << step << " time=" << time << '\n';

        amrex::UtilCreateCleanDirectory(out_chkname, false);

        for (int lev = 0; lev <= finest_level; ++lev) {
            const std::string level_dir = out_chkname + "/" + level_prefix + std::to_string(lev);
            amrex::UtilCreateDirectory(level_dir, 0755);
        }

        if (params_.write_particles) {
            amrex::UtilCreateDirectory(out_chkname + "/particles", 0755);
        }
    }
    ParallelDescriptor::Barrier();

    if (is_iop) {
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

    if (params_.write_particles) {
        if (mypc) {
            mypc->Checkpoint(out_chkname, "particles");
        }
    } else if (is_iop) {
        amrex::Print() << "[Checkpoint] Skip particle container (checkpoint.write_particles=0)\n";
    }
    ParallelDescriptor::Barrier();

    if (params_.keep_latest_only && is_iop) {
        DIR* dir = opendir(".");
        if (dir != nullptr) {
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
                if (!is_chk_dir(name)) {
                    continue;
                }

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
                       << "' (begin_step=" << params_.begin_step << ")\n";
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

    int finest_in_file = 0;
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
        vorticity[lev].define(boxArray(lev), DistributionMap(lev), 1, nghost);
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

    mypc.reset();
    mypc = std::make_unique<LagrangeParticleContainer>(this);
    if (params_.write_particles) {
        mypc->Restart(chkname, "particles");
    }

    ResetIDFCheckpointState();
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

        amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
            interp_scale(i, j, k, fold, fnew, scale);
        });
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
    regrid(0, cur_time);
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

void AmrCoreLBM::AverageDownValidLevel(int lev) {
    // amrex::AllPrint()<<"AverageDownValidLevel from " << lev+1 << " to " << lev <<std::endl;

    // amrex::average_down(f_old[lev+1], f_old[lev], geom[lev+1], geom[lev],0, Q, refRatio(lev));

    amrex::MultiFab& fine_mf = f_old[lev + 1];
    amrex::MultiFab& crse_mf = f_old[lev];

    MultiFab fine_boundary_data(fine_mf.boxArray(), fine_mf.DistributionMap(), Q, 0); // 能不能用f_new减少内存消耗
    MultiFab::Copy(fine_boundary_data, fine_mf, 0, 0, Q, 0);

    amrex::Real scale = 2.0 * tau[lev] / tau[lev + 1];

    for (MFIter mfi(fine_boundary_data, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const auto bx = mfi.growntilebox(0);

        const Array4<Real>& fold = fine_boundary_data.array(mfi);

        amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
            average_scale(i, j, k, fold, scale);
        });
    }

    amrex::average_down(fine_boundary_data, crse_mf, 0, Q, refRatio(lev));
}

void AmrCoreLBM::AverageDownValid() {
    for (int lev = finest_level - 1; lev >= 0; --lev) {
        AverageDownValidLevel(lev);
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

void AmrCoreLBM::AverageDownGhostLevel(int lev) {
    amrex::MultiFab& fine_mf = f_old[lev + 1];
    amrex::MultiFab& crse_mf = f_old[lev];

    MultiFab fine_boundary_data(fine_mf.boxArray(), fine_mf.DistributionMap(), Q, 2); // 能不能用f_new减少内存消耗
    MultiFab::Copy(fine_boundary_data, fine_mf, 0, 0, Q, 2);

    amrex::Real scale = 2.0 * tau[lev] / tau[lev + 1];

    for (MFIter mfi(fine_boundary_data, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const auto bx = mfi.growntilebox(2);

        const Array4<Real>& fold = fine_boundary_data.array(mfi);

        amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
            average_scale(i, j, k, fold, scale);
        });
    }

    amrex::average_down(fine_boundary_data, crse_mf, 0, Q, refRatio(lev));
}

void AmrCoreLBM::AverageDownGhost() {
}

void AmrCoreLBM::FillGhostLevel(int lev, amrex::Real time) {
    amrex::MultiFab& f_old_lev = f_old[lev];
    FillDdfPatch(lev, time, f_old_lev);
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
    amrex::MultiFab& f_old_lev = f_old[lev];
    f_old_lev.FillBoundary(geom[lev].periodicity());
}

void AmrCoreLBM::Boundary(int lev) {
    int right = Geom(lev).Domain().length(0) - 1;
    int up = Geom(lev).Domain().length(1) - 1;

    amrex::IntVect hi{right, up};

    amrex::MultiFab& f_old_lev = f_old[lev];
    amrex::MultiFab& f_new_lev = f_new[lev];

    for (MFIter mfi(f_old_lev, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const auto bx = mfi.growntilebox(nghost);
        const Array4<Real>& fold = f_old_lev.array(mfi);
        const Array4<Real>& fnew = f_new_lev.array(mfi);

        amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
            fill_boundary(i, j, k, fold, fnew, hi);
        });
    }
}

void AmrCoreLBM::Collide(int lev, int n) {
    int right = Geom(lev).Domain().length(0) - 1;
    int up = Geom(lev).Domain().length(1) - 1;

    amrex::IntVect hi{right, up};

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
            collide(i, j, k, fold, s, Ft, tau_lev, dt, hi);
        });
    }
}

void AmrCoreLBM::Stream(int lev, int n) {
    int right = Geom(lev).Domain().length(0) - 1;
    int up = Geom(lev).Domain().length(1) - 1;

    amrex::IntVect hi{right, up};

    amrex::MultiFab& f_old_lev = f_old[lev];
    amrex::MultiFab& f_new_lev = f_new[lev];

    bool is_finest = {lev == finest_level};
    bool is_coarsest = {lev == 0};

    for (MFIter mfi(f_old_lev, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const auto bx = mfi.growntilebox(n);
        const Array4<Real>& fold = f_old_lev.array(mfi);
        const Array4<Real>& fnew = f_new_lev.array(mfi);

        amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
            stream(i, j, k, fold, fnew, hi, is_finest);
        });
    }
}

void AmrCoreLBM::SwapLevel(int lev, int n) {
    amrex::MultiFab& f_old_lev = f_old[lev];
    amrex::MultiFab& f_new_lev = f_new[lev];

    for (MFIter mfi(f_old_lev, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const auto bx = mfi.growntilebox(n);
        const Array4<Real>& fold = f_old_lev.array(mfi);
        const Array4<Real>& fnew = f_new_lev.array(mfi);

        amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
            swap_ddf(i, j, k, fold, fnew);
        });
    }
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
    mypc = std::make_unique<LagrangeParticleContainer>(this);
    mypc->InitParticle(lev);
}

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

    mypc->ZeroParticleForce(lev);

    for (int iter = 0; iter < NF; iter++) {
        // 1*. 清零力增量（包括 ghost 区域）
        force_delta_lev.setVal(0.0, nghost);

        // 2. 计算本次迭代的力增量 → 写入 force_delta
        mypc->InterpForce(lev, rho_lev, u_lev, force_lev, force_delta_lev);

        // 3. 通信：ghost → valid（SumBoundary）
        force_delta_lev.SumBoundary(geom[lev].periodicity());

        // 4*. 累加 force_delta 到 force（只需要 valid 区域）
        amrex::MultiFab::Add(force_lev, force_delta_lev, 0, 0, AMREX_SPACEDIM, 0);

        // 5*. 通信：valid → ghost（FillBoundary）
        force_lev.FillBoundary(geom[lev].periodicity());

        // 调试：输出粒子力
        // if (do_debug) {
        //     mypc->DebugPrintForceSum(lev, call_count, iter);
        // }
    }
#else
    // 单次迭代（NF = 1）：直接使用 force，无需 force_delta
    mypc->InterpForce(lev, rho_lev, u_lev, force_lev);
    SumForce(lev);

#endif // USE_MDF_TWO_STAGE
}

void AmrCoreLBM::SumForce(int lev) {
    MultiFab* mf_pointer = &force[lev];
    mf_pointer->SumBoundary(Geom(lev).periodicity());
}

void AmrCoreLBM::ComputeParticle(int lev) {
    CommunicateLevel(lev);
    ComputeMacroLevel(lev);
    ApplyIDF(lev);
    // InterpForce(lev);
}

void AmrCoreLBM::ReduceFxy(int lev, int step) {
    mypc->SaveFxy(lev, step);
}

bool AmrCoreLBM::EvaluateConvergence(int lev, int step) {
    return mypc->EvaluateConvergence(lev, step);
}

void AmrCoreLBM::PrintParticleParm() {
    mypc->PrintParticleParm();
}

void AmrCoreLBM::RedistributeParticle() {
    // amrex::AllPrint()<< "RedistributeParticle" << std::endl;
    mypc->Redistribute();
    // 粒子在 rank 间迁移后，本地粒子计数 idf_local_NL 需要更新
    // 但对于静止边界，矩阵 A^(-1) 不变，只需更新本地计数
    idf_local_count_valid = false;
}

void AmrCoreLBM::InitCpPoint(int lev) {
}

void AmrCoreLBM::ComputeCp(int lev, int step) {
    amrex::MultiFab& rho_lev = density[lev];
    // 填充密度场的 ghost 区域，确保双线性插值时能访问边界外的正确数据
    rho_lev.FillBoundary(Geom(lev).periodicity());
    const std::string filename = "Cp_steady_step" + std::to_string(step) + ".dat";
    mypc->ComputeCp(lev, rho_lev, filename);
}

void AmrCoreLBM::ComputeCf(int lev, int step) {
    // 计算宏观量
    ComputeMacroLevel(lev);

    // 创建临时速度场，增加 ghost 层深度到 6，解决速度插值超出范围的问题
    amrex::MultiFab temp_velocity(velocity[lev].boxArray(), velocity[lev].DistributionMap(),
                                  AMREX_SPACEDIM, 6); // 6层ghost
    amrex::MultiFab::Copy(temp_velocity, velocity[lev], 0, 0, AMREX_SPACEDIM, 0);

    // 填充临时速度场的 ghost 层
    if (lev == 0) {
        // 最粗层：只填充同等级边界和周期性边界
        temp_velocity.FillBoundary(geom[lev].periodicity());
    } else {
        // 细化层：先从粗网格填充c-f边界，再填充同等级边界
        FillMacroPatch(lev, 0.0, temp_velocity);             // 粗-细网格插值
        temp_velocity.FillBoundary(geom[lev].periodicity()); // 同等级边界填充
    }

    // 计算局部摩擦系数
    const std::string filename = "Cf_steady_step" + std::to_string(step) + ".dat";
    mypc->ComputeCf(lev, temp_velocity, filename);
}

void AmrCoreLBM::ComputeCf_from_force_pressure(int lev, int step) {
    const std::string filename = "Cf_steady2_step" + std::to_string(step) + ".dat";
    mypc->ComputeCf_from_force_pressure(lev, filename);
}

// 构建活跃欧拉点集合：遍历所有拉格朗日点，将其 5x5 邻域内落在几何域（非ghost、非越界）的欧拉网格点加入集合（去重）
// 同时收集全局拉格朗日点位置用于后续矩阵组装
void AmrCoreLBM::BuildActiveEulerSet(int lev) {
    if (!mypc)
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

        for (MyParIter pti(*mypc, lev); pti.isValid(); ++pti) {
            const long n = pti.numParticles();
            if (n == 0)
                continue;
            idf_local_NL += n;
        }

        MPI_Allgather(&idf_local_NL, 1, MPI_INT,
                      idf_all_NL.data(), 1, MPI_INT,
                      ParallelDescriptor::Communicator());

        idf_local_count_valid = true;
        return;
    }

    const Geometry& gm = Geom(lev);
    const Box& domain = gm.Domain();
    const Real delta = Geom(lev).CellSize()[0];

    int nprocs = ParallelDescriptor::NProcs();
    int myrank = ParallelDescriptor::MyProc();

    // ========== Step 1: 收集本进程的拉格朗日点位置，同时计算本地活跃欧拉点 ==========
    std::vector<Real> local_lag_pos;   // [x0, y0, x1, y1, ...]
    std::vector<int> local_lag_ids;    // 粒子全局ID（用于排序）
    std::set<IntVect> local_euler_set; // 本进程的活跃欧拉点

    // 使用 LagrangeParticleContainer 的接口收集粒子数据
    idf_local_NL = mypc->IDF_CollectParticleData(lev, local_lag_pos, local_lag_ids);

    // 根据收集到的粒子位置计算活跃欧拉点
    for (int p = 0; p < idf_local_NL; ++p) {
        Real xt_phys = local_lag_pos[2 * p];
        Real yt_phys = local_lag_pos[2 * p + 1];
        Real xt = xt_phys / delta;
        Real yt = yt_phys / delta;

        for (int x = -2; x <= 2; x++) {
            for (int y = -2; y <= 2; y++) {
                int xx = static_cast<int>(amrex::Math::floor(xt)) + x;
                int yy = static_cast<int>(amrex::Math::floor(yt)) + y;
                const IntVect iv(xx, yy);

                if (!domain.contains(iv))
                    continue;

                Real lx = xt - (xx + 0.5);
                Real ly = yt - (yy + 0.5);
                Real IB_Interp = delta3pSmoothed(lx) * delta3pSmoothed(ly);

                if (IB_Interp > 1e-15) {
                    local_euler_set.insert(iv);
                }
            }
        }
    }

    // ========== Step 2: MPI 汇总拉格朗日点位置和ID（使用 Allgather/Allgatherv） ==========
    // 收集每个进程的粒子数量（Allgather 等长）
    idf_all_NL.resize(nprocs);
    MPI_Allgather(&idf_local_NL, 1, MPI_INT,
                  idf_all_NL.data(), 1, MPI_INT,
                  ParallelDescriptor::Communicator());

    // 计算偏移量
    std::vector<int> offsets(nprocs + 1, 0);
    std::partial_sum(idf_all_NL.begin(), idf_all_NL.end(), offsets.begin() + 1);
    idf_NL_global = offsets[nprocs];
    idf_local_offset = offsets[myrank]; // 保留用于兼容性

    // ===== Step 2a & 2b: 汇总所有粒子位置（按粒子ID直接定位，无需排序） =====
    // 方案 C: 利用粒子ID从1开始顺序分配的特性
    // 全局索引 = 粒子ID - 1，无需汇总ID和排序

    // 粒子位置有 x,y 两个坐标，需要 ×2
    std::vector<int> pos_recvcounts(nprocs), pos_displs(nprocs + 1, 0);
    for (int i = 0; i < nprocs; ++i) {
        pos_recvcounts[i] = idf_all_NL[i] * 2;
    }
    std::partial_sum(pos_recvcounts.begin(), pos_recvcounts.end(), pos_displs.begin() + 1);
    int total_pos = pos_displs[nprocs];
    std::vector<Real> unsorted_lag_pos(total_pos);
    // Allgatherv 汇总所有粒子位置到每个进程
    MPI_Allgatherv(local_lag_pos.data(), idf_local_NL * 2, ParallelDescriptor::Mpi_typemap<Real>::type(), unsorted_lag_pos.data(), pos_recvcounts.data(), pos_displs.data(),
                   ParallelDescriptor::Mpi_typemap<Real>::type(),
                   ParallelDescriptor::Communicator());

    // ===== Step 2c: 按粒子ID直接重排位置数据（无需排序） =====
    // 粒子 ID 从 1 开始，全局索引 = ID - 1
    idf_lag_pos_global.resize(idf_NL_global * 2);

    // 遍历 unsorted 数据，根据粒子 ID 直接写入正确位置
    // 需要先收集粒子 ID 来定位
    std::vector<int> all_lag_ids(idf_NL_global);
    // Allgatherv 汇总所有粒子 ID 到每个进程
    MPI_Allgatherv(local_lag_ids.data(), idf_local_NL, MPI_INT,
                   all_lag_ids.data(), idf_all_NL.data(), offsets.data(),
                   MPI_INT, ParallelDescriptor::Communicator());

    // 按粒子 ID 直接写入全局位置数组
    for (int i = 0; i < idf_NL_global; ++i) {
        int pid = all_lag_ids[i];
        int global_idx = pid - 1; // 粒子ID从1开始
        if (global_idx >= 0 && global_idx < idf_NL_global) {
            idf_lag_pos_global[2 * global_idx + 0] = unsorted_lag_pos[2 * i + 0];
            idf_lag_pos_global[2 * global_idx + 1] = unsorted_lag_pos[2 * i + 1];
        }
    }

    // （已不需要本地粒子全局索引列表；各阶段直接按粒子ID重排并使用全局向量）

    // ========== Step 3: MPI 汇总欧拉点（各进程先独立计算，再合并去重） ==========
    // 将本地欧拉点转为数组 [i0, j0, i1, j1, ...]
    std::vector<int> local_euler_data;
    for (const auto& iv : local_euler_set) {
        local_euler_data.push_back(iv[0]);
        local_euler_data.push_back(iv[1]);
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
        euler_recvcounts[i] = all_NE[i] * 2; // 每个欧拉点有 i,j 两个整数
        euler_displs[i] = total_euler_ints;
        total_euler_ints += euler_recvcounts[i];
    }

    // 汇总所有欧拉点数据
    std::vector<int> all_euler_data(total_euler_ints);
    MPI_Allgatherv(local_euler_data.data(), local_NE * 2, MPI_INT,
                   all_euler_data.data(), euler_recvcounts.data(), euler_displs.data(),
                   MPI_INT, ParallelDescriptor::Communicator());

    // 合并去重构建全局欧拉点集合
    std::set<IntVect> global_euler_set;
    for (int k = 0; k < total_euler_ints / 2; ++k) {
        IntVect iv(all_euler_data[2 * k], all_euler_data[2 * k + 1]);
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
        amrex::Print() << "[IDF] BuildActiveEulerSet: NL_global=" << idf_NL_global
                       << ", NE_global=" << idf_NE_global
                       << ", local_NL=" << idf_local_NL
                       << ", local_NE=" << local_NE
                       << ", local_offset=" << idf_local_offset << std::endl;
        first_call = false;
    }

    // 标记几何已构建（静止边界可复用）
    idf_geometry_built = true;
    idf_local_count_valid = true;
}

// ======================= IDF 实现 ========================== //

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
    if (!mypc)
        return;

    // Step 1: 构建活跃欧拉点集合和全局拉格朗日点位置
    BuildActiveEulerSet(lev);

    if (idf_NL_global == 0) {
        if (ParallelDescriptor::IOProcessor()) {
            amrex::Print() << "[IDF] Warning: No Lagrangian points found!" << std::endl;
        }
        return;
    }

    // BuildActiveEulerSet 已更新 idf_local_NL（仅首次构建）；这里再校验本 rank 粒子计数与缓冲长度一致
    long sum_n = 0;
    for (MyParIter pti(*mypc, lev); pti.isValid(); ++pti) {
        sum_n += pti.numParticles();
    }
    if (sum_n != idf_local_NL) {
        amrex::Print() << "[IDF][ERROR] local particle count mismatch: sum_n=" << sum_n
                       << " idf_local_NL=" << idf_local_NL << std::endl;
        amrex::Abort("IDF_InterpolateEulerToLag local count mismatch");
    }

    // Step 2: 使用 LagrangeParticleContainer 的接口执行插值
    amrex::MultiFab& u_lev = velocity[lev];
    amrex::MultiFab& rho_lev = density[lev];

    mypc->IDF_Interpolate(lev, u_lev, rho_lev);

    // Step 2b: 从粒子属性中读取插值结果到 CPU vector
    std::vector<Real> local_interp_ux, local_interp_uy, local_interp_rho;
    std::vector<int> local_interp_ids; // 同时收集粒子ID，用于重排
    mypc->IDF_ReadInterpResults(lev, local_interp_ux, local_interp_uy, local_interp_rho, &local_interp_ids);

    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(static_cast<int>(local_interp_ux.size()) == idf_local_NL,
                                     "IDF local buffer size mismatch");

    // Step 3: 汇总插值速度到全局向量（使用 Allgatherv 一步完成广播）
    int nprocs = ParallelDescriptor::NProcs();
    std::vector<int> interp_displs(nprocs + 1, 0);
    std::partial_sum(idf_all_NL.begin(), idf_all_NL.end(), interp_displs.begin() + 1);
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(interp_displs[nprocs] == idf_NL_global, "IDF global buffer size mismatch");

    // Step 3a: 汇总插值数据（ux, uy, rho, ids）
    std::vector<Real> unsorted_interp_ux(idf_NL_global), unsorted_interp_uy(idf_NL_global), unsorted_interp_rho(idf_NL_global);
    std::vector<int> all_interp_ids(idf_NL_global);

    MPI_Allgatherv(local_interp_ux.data(), idf_local_NL, ParallelDescriptor::Mpi_typemap<Real>::type(),
                   unsorted_interp_ux.data(), idf_all_NL.data(), interp_displs.data(),
                   ParallelDescriptor::Mpi_typemap<Real>::type(), ParallelDescriptor::Communicator());

    MPI_Allgatherv(local_interp_uy.data(), idf_local_NL, ParallelDescriptor::Mpi_typemap<Real>::type(),
                   unsorted_interp_uy.data(), idf_all_NL.data(), interp_displs.data(),
                   ParallelDescriptor::Mpi_typemap<Real>::type(), ParallelDescriptor::Communicator());

    MPI_Allgatherv(local_interp_rho.data(), idf_local_NL, ParallelDescriptor::Mpi_typemap<Real>::type(),
                   unsorted_interp_rho.data(), idf_all_NL.data(), interp_displs.data(),
                   ParallelDescriptor::Mpi_typemap<Real>::type(), ParallelDescriptor::Communicator());

    MPI_Allgatherv(local_interp_ids.data(), idf_local_NL, MPI_INT,
                   all_interp_ids.data(), idf_all_NL.data(), interp_displs.data(),
                   MPI_INT, ParallelDescriptor::Communicator());

    // Step 3b: 方案 C - 按粒子ID直接写入全局数组（无需排序）
    // 全局索引 = 粒子ID - 1
    idf_interp_u_x.resize(idf_NL_global);
    idf_interp_u_y.resize(idf_NL_global);
    idf_interp_rho.resize(idf_NL_global);
    for (int i = 0; i < idf_NL_global; ++i) {
        int pid = all_interp_ids[i];
        int global_idx = pid - 1; // 粒子ID从1开始
        if (global_idx >= 0 && global_idx < idf_NL_global) {
            idf_interp_u_x[global_idx] = unsorted_interp_ux[i];
            idf_interp_u_y[global_idx] = unsorted_interp_uy[i];
            idf_interp_rho[global_idx] = unsorted_interp_rho[i];
        }
    }

    // Step 4: 初始化目标速度（静止边界，u_target = 0）
    idf_target_u_x.assign(idf_NL_global, 0.0);
    idf_target_u_y.assign(idf_NL_global, 0.0);

    // 仅在第一次调用时输出
    static bool first_interp = true;
    if (first_interp && ParallelDescriptor::IOProcessor()) {
        amrex::Print() << "[IDF] Interpolated Euler velocity to Lagrangian points: NL_global="
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
    const Box& domain = gm.Domain();
    const Real delta = gm.CellSize()[0];

    int NL = idf_NL_global;
    int NE = idf_NE_global;

    // 分配 D_I (NL × NE) 和 D_E (NE × NL) 作为稀疏表示
    // D_I[i] = 第 i 个拉格朗日点的 <欧拉点索引, 权重> 列表
    // D_E[e] = 第 e 个欧拉点的 <拉格朗日点索引, 权重> 列表
    std::vector<std::vector<std::pair<int, Real>>> DI_rows(NL);
    std::vector<std::vector<std::pair<int, Real>>> DE_cols(NL);

    auto const& emap = idf_euler_index_map_global;

    // 基于全局拉格朗日点位置构建 D_I 和 D_E
    for (int p = 0; p < NL; ++p) {
        Real xp = idf_lag_pos_global[2 * p];
        Real yp = idf_lag_pos_global[2 * p + 1];
        Real xt = xp / delta;
        Real yt = yp / delta;
        int xx = static_cast<int>(amrex::Math::floor(xt));
        int yy = static_cast<int>(amrex::Math::floor(yt));

        for (int y = -2; y <= 2; ++y) {
            for (int x = -2; x <= 2; ++x) {
                IntVect iv(xx + x, yy + y);
                if (!domain.contains(iv))
                    continue;

                auto it = emap.find(iv);
                if (it == emap.end())
                    continue;

                int eidx = it->second;
                Real lx = xt - (iv[0] + 0.5);
                Real ly = yt - (iv[1] + 0.5);
                Real wI = delta3pSmoothed(lx) * delta3pSmoothed(ly);
                Real wE = wI * IB_weight;

                if (std::abs(wI) > 1e-15) {
                    DI_rows[p].emplace_back(eidx, wI);
                }
                if (std::abs(wE) > 1e-15) {
                    DE_cols[p].emplace_back(eidx, wE);
                }
            }
        }
    }

    // 将 D_E 按欧拉索引分组：e -> [(lag_idx, wE)]
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

    /*     if (ParallelDescriptor::IOProcessor()) {
            Real maxA = 0.0, sumDiag = 0.0;
            for (int i = 0; i < NL; ++i) {
                sumDiag += idf_A[i * NL + i];
                for (int j = 0; j < NL; ++j) {
                    maxA = std::max(maxA, std::abs(idf_A[i * NL + j]));
                }
            }
            amrex::Print() << "[IDF] Assembled A (NL=" << NL << " × NL=" << NL
                           << "), max|A|=" << maxA
                           << ", trace(A)=" << sumDiag << std::endl;

            //======== 输出矩阵到 dat 文件（方便导入 Excel） ========
            std::ofstream matrixFile("IDF_matrix_A.dat");
            if (matrixFile.is_open()) {
                matrixFile << std::scientific << std::setprecision(6);
                for (int i = 0; i < NL; ++i) {
                    for (int j = 0; j < NL; ++j) {
                        matrixFile << idf_A[i * NL + j];
                        if (j < NL - 1)
                            matrixFile << "\t"; // Tab 分隔，方便 Excel
                    }
                    matrixFile << "\n";
                }
                matrixFile.close();
                amrex::Print() << "[IDF] Matrix A exported to: IDF_matrix_A.dat" << std::endl;
            }
        } */

    // ======== 静止边界优化：预计算 A 的逆矩阵 ========
    if (!idf_inverse_built) {
        if (ParallelDescriptor::IOProcessor()) {
            amrex::Print() << "[IDF] Computing A^(-1) for static boundary..." << std::endl;
        }

        bool success = computeMatrixInverse(idf_A, idf_A_inv, NL);

        if (success) {
            idf_inverse_built = true;
            if (ParallelDescriptor::IOProcessor()) {
                Real maxAinv = 0.0;
                for (int i = 0; i < NL * NL; ++i) {
                    maxAinv = std::max(maxAinv, std::abs(idf_A_inv[i]));
                }
                amrex::Print() << "[IDF] A^(-1) computed successfully, max|A^(-1)|="
                               << maxAinv << std::endl;
            }
        } else {
            if (ParallelDescriptor::IOProcessor()) {
                amrex::Print() << "[IDF] Warning: A^(-1) computation failed, will use iterative solver" << std::endl;
            }
        }
    }
}

// 使用预计算的 A^(-1) 或迭代求解，计算力并写回粒子属性
void AmrCoreLBM::IDF_SolveSystem(int lev) {
    int NL = idf_NL_global;
    if (NL == 0)
        return;

    // ====== Step 1: 使用插值后的密度 ======
    // 密度已在 IDF_InterpolateEulerToLag 中通过 ibm_interpolate 插值并存储到 idf_interp_rho

    // ====== Step 2: 构建 RHS: b = u_interp - u_target ======表示颗粒的受力
    idf_rhs_x.resize(NL);
    idf_rhs_y.resize(NL);
    for (int i = 0; i < NL; ++i) {
        idf_rhs_x[i] = idf_interp_u_x[i] - idf_target_u_x[i];
        idf_rhs_y[i] = idf_interp_u_y[i] - idf_target_u_y[i];
    }

    // ====== Step 3: 求解线性系统 A * f_vel = rhs 并同时计算力密度 ======
    idf_sol_x.resize(NL);
    idf_sol_y.resize(NL);

    if (idf_inverse_built) {
        // 使用预计算的 A^(-1)：矩阵-向量乘法并同时应用密度系数
        // idf_sol = rhot_coeff * (A^(-1) * rhs)
        denseMatVecWithScale(idf_A_inv, idf_rhs_x, idf_interp_rho, idf_sol_x);
        denseMatVecWithScale(idf_A_inv, idf_rhs_y, idf_interp_rho, idf_sol_y);

        // if (ParallelDescriptor::IOProcessor()) {
        //     static int call_count = 0;
        //     if (call_count % 1000 == 0) {
        //         amrex::Print() << "[IDF] Using precomputed A^(-1), step=" << call_count << std::endl;
        //     }
        //     call_count++;
        // }
    } else {
        // // 回退到迭代求解
        // if (ParallelDescriptor::IOProcessor()) {
        //     amrex::Print() << "[IDF] Solving A * fx = rhs_x using BiCGSTAB..." << std::endl;
        // }
        // bool conv_x = bicgstabSolve(idf_A, idf_rhs_x, idf_sol_x, 2000, 1e-10);

        // if (ParallelDescriptor::IOProcessor()) {
        //     amrex::Print() << "[IDF] Solving A * fy = rhs_y using BiCGSTAB..." << std::endl;
        // }
        // bicgstabSolve(idf_A, idf_rhs_y, idf_sol_y, 2000, 1e-10);

        // // 迭代求解后应用密度系数
        // for (int i = 0; i < NL; ++i) {
        //     idf_sol_x[i] = rhot_coeff[i] * idf_sol_x[i];
        //     idf_sol_y[i] = rhot_coeff[i] * idf_sol_y[i];
        // }
    }

    // ====== Step 4: 使用 LagrangeParticleContainer 的接口将力写回粒子属性 ======
    // 方案 C: 使用 idf_NL_global 而非 idf_pid_to_global_idx
    mypc->IDF_WriteForceToParticles(lev, idf_sol_x, idf_sol_y, idf_NL_global);

    // if (ParallelDescriptor::IOProcessor()) {
    //     static int solve_count = 0;
    //     if (solve_count % 1000 == 0) {
    //         Real max_fx = 0.0, max_fy = 0.0;
    //         for (int i = 0; i < NL; ++i) {
    //             max_fx = std::max(max_fx, std::abs(idf_sol_x[i]));
    //             max_fy = std::max(max_fy, std::abs(idf_sol_y[i]));
    //         }
    //         amrex::Print() << "|f_density_x|_max=" << max_fx
    //                        << ", |f_density_y|_max=" << max_fy << std::endl;
    //     }
    //     solve_count++;
    // }
}

// 传播拉格朗日力到欧拉网格
void AmrCoreLBM::IDF_SpreadLagToEuler(int lev) {
    if (idf_NL_global == 0)
        return;

    amrex::MultiFab& force_lev = force[lev];
    force_lev.setVal(0.0, nghost);

    // 使用 LagrangeParticleContainer 接口传播力
    mypc->IDF_SpreadForce(lev, force_lev);

    // 仅在第一次调用时输出
    static bool first_spread = true;
    if (first_spread && ParallelDescriptor::IOProcessor()) {
        amrex::Print() << "[IDF] Spread Lagrangian forces to Euler grid done." << std::endl;
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
    vort_lev.define(ba, dm, 1, nghost);
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
    amrex::MultiFab vort_new(ba, dm, 1, nghost);
    amrex::MultiFab force_new(ba, dm, AMREX_SPACEDIM, nghost);
    amrex::MultiFab shear_new(ba, dm, 1, nghost);
    // force_delta 现在仅作为临时变量在 InterpForce() 中创建，不需在此分配

    FillDdfPatch(lev, time, old_state);

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
    vort_lev.define(ba, dm, 1, nghost);
    force_lev.define(ba, dm, AMREX_SPACEDIM, nghost);
    shear_lev.define(ba, dm, 1, nghost);
    f_new_lev.define(ba, dm, Q, nghost);
    f_old_lev.define(ba, dm, Q, nghost);
    // force_delta 现在仅作为临时变量在 InterpForce() 中创建，不需在此分配

    force_lev.setVal(0.0, nghost); // 在这里归零会不会好一点
    shear_lev.setVal(0.0, nghost);
    vort_lev.setVal(0.0, nghost);

    // bool printed_stride = false;
    for (MFIter mfi(f_old_lev, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const Box& bx = mfi.growntilebox(nghost);
        Array4<Real> const& fold = f_old_lev.array(mfi);
        Array4<Real> const& fnew = f_new_lev.array(mfi);
        /*** 打印stride仅供调试
                if (!printed_stride && Q >= 2) {
                    const FArrayBox& fab = f_old_lev[mfi];
                    std::ptrdiff_t delta = fab.dataPtr(1) - fab.dataPtr(0);
                    amrex::Print() << "[DEBUG] component stride = " << delta
                                   << ", ncell = " << fab.box().numPts() << "\n";
                    printed_stride = true;
                }
         */

        amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
            init_fluid(i, j, k, fold, fnew);
        });
    }
}

void AmrCoreLBM::ErrorEst(int lev, amrex::TagBoxArray& tags, amrex::Real time, int ngrow) {
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
    amrex::Real dx = Geom(lev).CellSizeArray()[0];

    amrex::IntVect lo1 = static_lo[lev];
    amrex::IntVect hi1 = static_hi[lev];

    amrex::IntVect lo2 = static_lo[lev + max_ref_level + 1];
    amrex::IntVect hi2 = static_hi[lev + max_ref_level + 1];

    for (MFIter mfi(f_old_lev, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        const Box& bx = mfi.growntilebox(0);
        const auto vort = vort_lev.array(mfi);
        const auto tagfab = tags.array(mfi);

        const IntVect& lo = bx.smallEnd();
        const IntVect& hi = bx.bigEnd();

        Real err_value = err[lev];

        amrex::ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
            state_error(i, j, k, tagfab, vort, err_value, tagval, clearval, lev, dx, lo2, hi2);
        });
    }
}
