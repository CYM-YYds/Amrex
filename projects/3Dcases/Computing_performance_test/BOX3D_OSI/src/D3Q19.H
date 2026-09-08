#ifndef D3Q19_H_
#define D3Q19_H_

#include <AMReX_Array4.H>
#include <AMReX_RealVect.H>

#define PI (3.141592653589793238462643383279)

using namespace amrex;

#define DIM 3
#define Q 27

const int coarsest_level = 0;
const int max_ref_level = 3;
const int rate = (1 << max_ref_level);

/*-----------------复杂结构体------------------------*/
// const Real obj_lo_x = -16.0;
// const Real obj_hi_x = 16.0;

// const Real obj_lo_y = -67.0;
// const Real obj_hi_y = 5.0;

// const Real obj_lo_z = -4.0;
// const Real obj_hi_z = 20.0;

// const Real obj_dim_x = (obj_hi_x  - obj_lo_x);
// const Real obj_dim_y = (obj_hi_y  - obj_lo_y);
// const Real obj_dim_z = (obj_hi_z  - obj_lo_z);
/*---------------------------------------------------*/

const Real D = 32.0; // obj_dim_z;
const Real R = D / 2.0;

AMREX_GPU_MANAGED inline int NX = 4 * static_cast<int>(D);
AMREX_GPU_MANAGED inline int NY = 4 * static_cast<int>(D);
AMREX_GPU_MANAGED inline int NZ = 4 * static_cast<int>(D);

AMREX_GPU_MANAGED inline Real nx = NX;
AMREX_GPU_MANAGED inline Real ny = NY;
AMREX_GPU_MANAGED inline Real nz = NZ;

#define dx_0 (nx / NX)
#define dt_0 (dx_0)
#define c (1.0)
#define cs (c / sqrt(3.0))
#define cs2 (cs * cs)
#define rho0 (1.0) //(p0 / cs2)
#define p0 (rho0 * cs2)


/*---------圆球绕流--------------------------*/
#define Re (1000)
#define U0 (0.05)
#define Ma (U0 / cs)
#define mv_0 (U0 * NX * dx_0 / Re)
#define tau_0 (mv_0 / (cs2 * dt_0) + 0.5)
#define G (981)
/*-----------颗粒流--------------------------*/
// #define Re    (1.5)
// #define U0    (1.04375) //(Ma * cs)
// #define mv_0  (0.004589623)
// #define tau_0 (mv_0 / (cs2 * dt_0) + 0.5)
// #define G     (0.001878036)
/*-------------------------------------------*/

#define rhof (rho0)
#define rhop (1.14 * rho0)
#define volume (PI * D * dx_0 * D * dx_0 * D * dx_0 / 6.0)
#define Mp (rhop * volume) // g
#define Mf (rhof * volume) // g
#define Mp_iner (Mp * D * dx_0 * D * dx_0 / 10.0)
#define Mf_iner (Mf * D * dx_0 * D * dx_0 / 10.0)

AMREX_GPU_MANAGED inline Real X = NX / 2.0;
AMREX_GPU_MANAGED inline Real Y = NY / 2.0;
AMREX_GPU_MANAGED inline Real Z = NZ / 2.0;
AMREX_GPU_MANAGED inline Real center[DIM] = {X, Y, Z};

// 颗粒相关的参数
AMREX_GPU_MANAGED inline Real dx_min = dx_0 / rate;
AMREX_GPU_MANAGED inline Real dt_min = dt_0 / rate;

inline void ConfigureLbmGrid(int nx_cells, int ny_cells, int nz_cells,
                             Real x_length, Real y_length, Real z_length)
{
    AMREX_ALWAYS_ASSERT(nx_cells > 0 && ny_cells > 0 && nz_cells > 0);
    NX = nx_cells;
    NY = ny_cells;
    NZ = nz_cells;
    nx = x_length;
    ny = y_length;
    nz = z_length;
    X = NX / 2.0;
    Y = NY / 2.0;
    Z = NZ / 2.0;
    center[0] = X;
    center[1] = Y;
    center[2] = Z;
    dx_min = dx_0 / rate;
    dt_min = dt_0 / rate;
}

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

// D3Q19-opt
// AMREX_GPU_DEVICE AMREX_FORCE_INLINE
//     IntVect e[Q] =
//         {
//             {0, 0, 0},
//             {1, 0, 0},
//             {-1, 0, 0},
//             {0, 1, 0},
//             {0, -1, 0},
//             {0, 0, 1},
//             {0, 0, -1},
//             {1, 1, 0},
//             {-1, -1, 0},
//             {1, 0, 1},
//             {-1, 0, -1},
//             {0, 1, 1},
//             {0, -1, -1},
//             {1, -1, 0},
//             {-1, 1, 0},
//             {1, 0, -1},
//             {-1, 0, 1},
//             {0, 1, -1},
//             {0, -1, 1}};

// D3Q19
// AMREX_GPU_DEVICE AMREX_FORCE_INLINE
//     IntVect e[Q] =
//         {
//             {0, 0, 0},
//             {1, 0, 0},
//             {-1, 0, 0},
//             {0, 1, 0},
//             {0, -1, 0},
//             {0, 0, 1},
//             {0, 0, -1},
//             {1, 1, 0},
//             {-1, -1, 0},
//             {1, -1, 0},
//             {-1, 1, 0},
//             {1, 0, 1},
//             {-1, 0, -1},
//             {1, 0, -1},
//             {-1, 0, 1},
//             {0, 1, 1},
//             {0, -1, -1},
//             {0, 1, -1},
//             {0, -1, 1}};

// AMREX_GPU_DEVICE AMREX_FORCE_INLINE
//     Real w[Q] =
//         {
//             1. / 3.,
//             1. / 18.,
//             1. / 18.,
//             1. / 18.,
//             1. / 18.,
//             1. / 18.,
//             1. / 18.,
//             1. / 36.,
//             1. / 36.,
//             1. / 36.,
//             1. / 36.,
//             1. / 36.,
//             1. / 36.,
//             1. / 36.,
//             1. / 36.,
//             1. / 36.,
//             1. / 36.,
//             1. / 36.,
//             1. / 36.};

// //D3Q27_CUMULANT
AMREX_GPU_DEVICE AMREX_FORCE_INLINE
    IntVect e[Q] =
        {
            {0, 0, 0},
            {0, 1, 0},
            {0, -1, 0},
            {-1, 0, 0},
            {1, 0, 0},
            {0, 0, 1},
            {0, 0, -1},
            {-1, 1, 0},
            {1, 1, 0},
            {-1, -1, 0},
            {1, -1, 0},
            {0, 1, 1},
            {0, -1, 1},
            {-1, 0, 1},
            {1, 0, 1},
            {0, 1, -1},
            {0, -1, -1},
            {-1, 0, -1},
            {1, 0, -1},
            {1, 1, 1},
            {-1, 1, 1},
            {1, -1, 1},
            {-1, -1, 1},
            {1, 1, -1},
            {-1, 1, -1},
            {1, -1, -1},
            {-1, -1, -1}};

AMREX_GPU_DEVICE AMREX_FORCE_INLINE
    Real w[Q] =
        {
            8. / 27.,
            2. / 27.,
            2. / 27.,
            2. / 27.,
            2. / 27.,
            2. / 27.,
            2. / 27.,
            1. / 54.,
            1. / 54.,
            1. / 54.,
            1. / 54.,
            1. / 54.,
            1. / 54.,
            1. / 54.,
            1. / 54.,
            1. / 54.,
            1. / 54.,
            1. / 54.,
            1. / 54.,
            1. / 216.,
            1. / 216.,
            1. / 216.,
            1. / 216.,
            1. / 216.,
            1. / 216.,
            1. / 216.,
            1. / 216.};

// D3Q27_CUMULANT_OPT
//  AMREX_GPU_DEVICE AMREX_FORCE_INLINE
//  IntVect e[Q] =
//  {
//      {  0,  0,  0 },
//      {  1,  0,  0 },
//      { -1,  0,  0 },
//      {  0,  1,  0 },
//      {  0, -1,  0 },
//      {  0,  0,  1 },
//      {  0,  0, -1 },
//      {  1,  1,  0 },
//      { -1, -1,  0 },
//      {  1, -1,  0 },
//      { -1,  1,  0 },
//      {  1,  0,  1 },
//      { -1,  0, -1 },
//      {  1,  0, -1 },
//      { -1,  0,  1 },
//      {  0,  1,  1 },
//      {  0, -1, -1 },
//      {  0,  1, -1 },
//      {  0, -1,  1 },
//      {  1,  1,  1 },
//      { -1,  1,  1 },
//      {  1, -1,  1 },
//      { -1, -1,  1 },
//      {  1,  1, -1 },
//      { -1,  1, -1 },
//      {  1, -1, -1 },
//      { -1, -1, -1 }
//  };

// AMREX_GPU_DEVICE AMREX_FORCE_INLINE
// Real w[Q] =
// {
//     8./27.,
//     2./27.,
//     2./27.,
//     2./27.,
//     2./27.,
//     2./27.,
//     2./27.,
//     1./54.,
//     1./54.,
//     1./54.,
//     1./54.,
//     1./54.,
//     1./54.,
//     1./54.,
//     1./54.,
//     1./54.,
//     1./54.,
//     1./54.,
//     1./54.,
//     1./216.,
//     1./216.,
//     1./216.,
//     1./216.,
//     1./216.,
//     1./216.,
//     1./216.,
//     1./216.
// };

AMREX_GPU_DEVICE AMREX_FORCE_INLINE
    Real
    feqHeLuo(const Real& pressure, const RealVect& u, const int i) {
    amrex::Real eu, uv, feqt;

    eu = e[i][0] * u[0] + e[i][1] * u[1] + e[i][2] * u[2];

    uv = (u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);

    feqt = w[i] * (pressure + p0 * (3.0 * eu + 4.5 * eu * eu - 1.5 * uv));

    return feqt;
}

AMREX_GPU_DEVICE AMREX_FORCE_INLINE
    Real
    feqQian(const Real& rhot, const RealVect& u, const int i) {
    amrex::Real eu, uv, feqt;

    eu = e[i][0] * u[0] + e[i][1] * u[1] + e[i][2] * u[2];

    uv = (u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);

    feqt = w[i] * rhot * (1.0 + 3.0 * eu + 4.5 * eu * eu - 1.5 * uv);

    return feqt;
}

AMREX_GPU_DEVICE AMREX_FORCE_INLINE
    Real
    forceGuo(const RealVect& u, const RealVect& Ft, const int i) {
    amrex::Real m, n, nf;

    m = ((e[i][0] - u[0]) * Ft[0] + (e[i][1] - u[1]) * Ft[1] + (e[i][2] - u[2]) * Ft[2]) / cs2;

    n = (e[i][0] * u[0] + e[i][1] * u[1] + e[i][2] * u[2]) / cs2;

    nf = (e[i][0] * Ft[0] + e[i][1] * Ft[1] + e[i][2] * Ft[2]) / cs2;

    return w[i] * (m + n * nf);
}

#endif
