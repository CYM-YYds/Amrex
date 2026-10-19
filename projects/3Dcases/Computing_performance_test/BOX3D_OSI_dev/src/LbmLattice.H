#ifndef LBM_LATTICE_H_
#define LBM_LATTICE_H_

#include <AMReX_Array4.H>
#include <AMReX_RealVect.H>

#define PI (3.141592653589793238462643383279)

using namespace amrex;

// 维度唯一来自 AMReX 构建配置，二维使用 D2Q9，三维使用 D3Q27。
#define LBM_DIM AMREX_SPACEDIM
#if AMREX_SPACEDIM == 2
#define LBM_Q 9
#elif AMREX_SPACEDIM == 3
#define LBM_Q 27
#else
#error "Only DIM=2 and DIM=3 are supported"
#endif
#define LBM_ORDER 0

#define DIM LBM_DIM
#define Q LBM_Q
static_assert(LBM_DIM == AMREX_SPACEDIM, "LBM dimension must match AMReX build");

const int coarsest_level = 0;
const int max_ref_level = 3;
const int rate = (1 << max_ref_level);

const Real D = 32.0; // obj_dim_z;
const Real R = D / 2.0;

#define c (1.0)
#define cs (c / sqrt(3.0))
#define cs2 (cs * cs)
#define rho0 (1.0) //(p0 / cs2)
#define p0 (rho0 * cs2)

/*---------圆球绕流--------------------------*/
#define U0 (0.05)
#define Ma (U0 / cs)
#define G (981)
/*-----------颗粒流--------------------------*/
// #define U0    (1.04375) //(Ma * cs)
// #define mv_0  (0.004589623)
// #define tau_0 (mv_0 / (cs2 * dt_0) + 0.5)
// #define G     (0.001878036)
/*-------------------------------------------*/

#define rhof (rho0)
#define rhop (1.14 * rho0)
const Real rb = pow(((pow(rate * R, 3) + pow(rate * R - 1, 3)) / 2.0), (1.0 / 3.0)); // 修正的半径

const Real ds0 = 1.0;
const int ns = PI * rb / ds0 + 1;  // 表面分带数
const int nt1 = 2 * PI * rb / ds0; // 赤道上point数量

const Real LP_dr = 1.0;
const Real LP_area = 4 * PI * rb * rb;

// 多颗粒相关的参数
const Real safe = 0.025 * R;
const Real Epp1 = 0.25;
const Real Epp2 = 0.02;
const Real Epw1 = 0.125;
const Real Epw2 = 0.01;

// 方向与权重绑定，host/device 表由同一列表生成，避免调整排列时遗漏权重。
// 排列 0：原 D3Q19 / 当前 D3Q27；排列 1：原 D3Q19-opt / D3Q27_CUMULANT_OPT。
// D2Q9 只提供排列 0；其他组合明确拒绝，不自动推导未知模型。
#define LBM_D3Q19_ORDER0(X) \
    X(0, 0, 0, 1.0 / 3.0) \
    X(1, 0, 0, 1.0 / 18.0) \
    X(-1, 0, 0, 1.0 / 18.0) \
    X(0, 1, 0, 1.0 / 18.0) \
    X(0, -1, 0, 1.0 / 18.0) \
    X(0, 0, 1, 1.0 / 18.0) \
    X(0, 0, -1, 1.0 / 18.0) \
    X(1, 1, 0, 1.0 / 36.0) \
    X(-1, -1, 0, 1.0 / 36.0) \
    X(1, -1, 0, 1.0 / 36.0) \
    X(-1, 1, 0, 1.0 / 36.0) \
    X(1, 0, 1, 1.0 / 36.0) \
    X(-1, 0, -1, 1.0 / 36.0) \
    X(1, 0, -1, 1.0 / 36.0) \
    X(-1, 0, 1, 1.0 / 36.0) \
    X(0, 1, 1, 1.0 / 36.0) \
    X(0, -1, -1, 1.0 / 36.0) \
    X(0, 1, -1, 1.0 / 36.0) \
    X(0, -1, 1, 1.0 / 36.0)

#define LBM_D3Q19_ORDER1(X) \
    X(0, 0, 0, 1.0 / 3.0) \
    X(1, 0, 0, 1.0 / 18.0) \
    X(-1, 0, 0, 1.0 / 18.0) \
    X(0, 1, 0, 1.0 / 18.0) \
    X(0, -1, 0, 1.0 / 18.0) \
    X(0, 0, 1, 1.0 / 18.0) \
    X(0, 0, -1, 1.0 / 18.0) \
    X(1, 1, 0, 1.0 / 36.0) \
    X(-1, -1, 0, 1.0 / 36.0) \
    X(1, 0, 1, 1.0 / 36.0) \
    X(-1, 0, -1, 1.0 / 36.0) \
    X(0, 1, 1, 1.0 / 36.0) \
    X(0, -1, -1, 1.0 / 36.0) \
    X(1, -1, 0, 1.0 / 36.0) \
    X(-1, 1, 0, 1.0 / 36.0) \
    X(1, 0, -1, 1.0 / 36.0) \
    X(-1, 0, 1, 1.0 / 36.0) \
    X(0, 1, -1, 1.0 / 36.0) \
    X(0, -1, 1, 1.0 / 36.0)

#define LBM_D3Q27_ORDER0(X) \
    X(0, 0, 0, 8.0 / 27.0) \
    X(0, 1, 0, 2.0 / 27.0) \
    X(0, -1, 0, 2.0 / 27.0) \
    X(-1, 0, 0, 2.0 / 27.0) \
    X(1, 0, 0, 2.0 / 27.0) \
    X(0, 0, 1, 2.0 / 27.0) \
    X(0, 0, -1, 2.0 / 27.0) \
    X(-1, 1, 0, 1.0 / 54.0) \
    X(1, 1, 0, 1.0 / 54.0) \
    X(-1, -1, 0, 1.0 / 54.0) \
    X(1, -1, 0, 1.0 / 54.0) \
    X(0, 1, 1, 1.0 / 54.0) \
    X(0, -1, 1, 1.0 / 54.0) \
    X(-1, 0, 1, 1.0 / 54.0) \
    X(1, 0, 1, 1.0 / 54.0) \
    X(0, 1, -1, 1.0 / 54.0) \
    X(0, -1, -1, 1.0 / 54.0) \
    X(-1, 0, -1, 1.0 / 54.0) \
    X(1, 0, -1, 1.0 / 54.0) \
    X(1, 1, 1, 1.0 / 216.0) \
    X(-1, 1, 1, 1.0 / 216.0) \
    X(1, -1, 1, 1.0 / 216.0) \
    X(-1, -1, 1, 1.0 / 216.0) \
    X(1, 1, -1, 1.0 / 216.0) \
    X(-1, 1, -1, 1.0 / 216.0) \
    X(1, -1, -1, 1.0 / 216.0) \
    X(-1, -1, -1, 1.0 / 216.0)

#define LBM_D3Q27_ORDER1(X) \
    X(0, 0, 0, 8.0 / 27.0) \
    X(1, 0, 0, 2.0 / 27.0) \
    X(-1, 0, 0, 2.0 / 27.0) \
    X(0, 1, 0, 2.0 / 27.0) \
    X(0, -1, 0, 2.0 / 27.0) \
    X(0, 0, 1, 2.0 / 27.0) \
    X(0, 0, -1, 2.0 / 27.0) \
    X(1, 1, 0, 1.0 / 54.0) \
    X(-1, -1, 0, 1.0 / 54.0) \
    X(1, -1, 0, 1.0 / 54.0) \
    X(-1, 1, 0, 1.0 / 54.0) \
    X(1, 0, 1, 1.0 / 54.0) \
    X(-1, 0, -1, 1.0 / 54.0) \
    X(1, 0, -1, 1.0 / 54.0) \
    X(-1, 0, 1, 1.0 / 54.0) \
    X(0, 1, 1, 1.0 / 54.0) \
    X(0, -1, -1, 1.0 / 54.0) \
    X(0, 1, -1, 1.0 / 54.0) \
    X(0, -1, 1, 1.0 / 54.0) \
    X(1, 1, 1, 1.0 / 216.0) \
    X(-1, 1, 1, 1.0 / 216.0) \
    X(1, -1, 1, 1.0 / 216.0) \
    X(-1, -1, 1, 1.0 / 216.0) \
    X(1, 1, -1, 1.0 / 216.0) \
    X(-1, 1, -1, 1.0 / 216.0) \
    X(1, -1, -1, 1.0 / 216.0) \
    X(-1, -1, -1, 1.0 / 216.0)

#define LBM_D2Q9_ORDER0(X) \
    X(0, 0, 0, 4.0 / 9.0) \
    X(1, 0, 0, 1.0 / 9.0) \
    X(-1, 0, 0, 1.0 / 9.0) \
    X(0, 1, 0, 1.0 / 9.0) \
    X(0, -1, 0, 1.0 / 9.0) \
    X(1, 1, 0, 1.0 / 36.0) \
    X(-1, -1, 0, 1.0 / 36.0) \
    X(1, -1, 0, 1.0 / 36.0) \
    X(-1, 1, 0, 1.0 / 36.0)

#if LBM_DIM == 3 && LBM_Q == 19 && LBM_ORDER == 0
#define LBM_DIRECTIONS(X) LBM_D3Q19_ORDER0(X)
#elif LBM_DIM == 3 && LBM_Q == 19 && LBM_ORDER == 1
#define LBM_DIRECTIONS(X) LBM_D3Q19_ORDER1(X)
#elif LBM_DIM == 3 && LBM_Q == 27 && LBM_ORDER == 0
#define LBM_DIRECTIONS(X) LBM_D3Q27_ORDER0(X)
#elif LBM_DIM == 3 && LBM_Q == 27 && LBM_ORDER == 1
#define LBM_DIRECTIONS(X) LBM_D3Q27_ORDER1(X)
#elif LBM_DIM == 2 && LBM_Q == 9 && LBM_ORDER == 0
#define LBM_DIRECTIONS(X) LBM_D2Q9_ORDER0(X)
#else
#error "Unsupported LBM dimension, Q or ordering"
#endif

#define LBM_DIRECTION(x, y, z, weight) {AMREX_D_DECL(x, y, z)},
inline constexpr int e_host[][LBM_DIM] = {
    LBM_DIRECTIONS(LBM_DIRECTION)};
static_assert(sizeof(e_host) / sizeof(e_host[0]) == Q,
              "LBM direction count must match Q");

AMREX_GPU_DEVICE AMREX_FORCE_INLINE
IntVect e[Q] = {
    LBM_DIRECTIONS(LBM_DIRECTION)};
#undef LBM_DIRECTION

// 第三速度分量统一从原方向表读取；二维返回零，不访问不存在的分量。
// GPU 与主机入口分开，避免跨地址空间访问；不新增方向表或派生状态。
AMREX_GPU_DEVICE AMREX_FORCE_INLINE
int lattice_ez(int q) noexcept {
    return AMREX_D_PICK(0, 0, e[q][2]);
}

constexpr int lattice_ez_host(int q) noexcept {
    return AMREX_D_PICK(0, 0, e_host[q][2]);
}

#define LBM_WEIGHT(x, y, z, weight) Real(weight),
AMREX_GPU_DEVICE AMREX_FORCE_INLINE
Real w[Q] = {
    LBM_DIRECTIONS(LBM_WEIGHT)};
#undef LBM_WEIGHT
#undef LBM_DIRECTIONS
#undef LBM_D3Q19_ORDER0
#undef LBM_D3Q19_ORDER1
#undef LBM_D3Q27_ORDER0
#undef LBM_D3Q27_ORDER1
#undef LBM_D2Q9_ORDER0

AMREX_GPU_DEVICE AMREX_FORCE_INLINE
    Real
    feqHeLuo(const Real& pressure, const RealVect& u, const int i) {
    amrex::Real eu, uv, feqt;

#if LBM_DIM == 3
    // 保留三维原有的显式表达式与运算顺序，便于逐值回归。
    eu = e[i][0] * u[0] + e[i][1] * u[1] + lattice_ez(i) * u[2];

    uv = (u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
#else
    // 按所选维度求内积，仅保证本文件公式不固定访问第三分量。
    eu = Real(0);
    uv = Real(0);
    for (int d = 0; d < LBM_DIM; ++d) {
        eu += e[i][d] * u[d];
        uv += u[d] * u[d];
    }
#endif

    feqt = w[i] * (pressure + p0 * (3.0 * eu + 4.5 * eu * eu - 1.5 * uv));

    return feqt;
}

AMREX_GPU_DEVICE AMREX_FORCE_INLINE
    Real
    feqQian(const Real& rhot, const RealVect& u, const int i) {
    amrex::Real eu, uv, feqt;

#if LBM_DIM == 3
    // 保留三维原有的显式表达式与运算顺序，便于逐值回归。
    eu = e[i][0] * u[0] + e[i][1] * u[1] + lattice_ez(i) * u[2];

    uv = (u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
#else
    // 按所选维度求内积，仅保证本文件公式不固定访问第三分量。
    eu = Real(0);
    uv = Real(0);
    for (int d = 0; d < LBM_DIM; ++d) {
        eu += e[i][d] * u[d];
        uv += u[d] * u[d];
    }
#endif

    feqt = w[i] * rhot * (1.0 + 3.0 * eu + 4.5 * eu * eu - 1.5 * uv);

    return feqt;
}

AMREX_GPU_DEVICE AMREX_FORCE_INLINE
    Real
    forceGuo(const RealVect& u, const RealVect& Ft, const int i) {
    amrex::Real m, n, nf;

#if LBM_DIM == 3
    m = ((e[i][0] - u[0]) * Ft[0] + (e[i][1] - u[1]) * Ft[1] + (lattice_ez(i) - u[2]) * Ft[2]) / cs2;

    n = (e[i][0] * u[0] + e[i][1] * u[1] + lattice_ez(i) * u[2]) / cs2;

    nf = (e[i][0] * Ft[0] + e[i][1] * Ft[1] + lattice_ez(i) * Ft[2]) / cs2;

#else
    // 先累加内积再除以声速平方，保持原公式的运算结构。
    m = Real(0);
    n = Real(0);
    nf = Real(0);
    for (int d = 0; d < LBM_DIM; ++d) {
        m += (e[i][d] - u[d]) * Ft[d];
        n += e[i][d] * u[d];
        nf += e[i][d] * Ft[d];
    }
    m /= cs2;
    n /= cs2;
    nf /= cs2;

#endif

    return w[i] * (m + n * nf);
}

#endif
