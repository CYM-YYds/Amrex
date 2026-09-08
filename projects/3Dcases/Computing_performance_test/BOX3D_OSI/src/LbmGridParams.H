#ifndef BOX3D_OSI_LBM_GRID_PARAMS_H_
#define BOX3D_OSI_LBM_GRID_PARAMS_H_

#include <AMReX_REAL.H>

// Small, trivially-copyable runtime grid description for host/GPU parameter passing.
struct LbmGridParams {
    int nx_cells = 0;
    int ny_cells = 0;
    int nz_cells = 0;
    amrex::Real x_length = 0.0;
    amrex::Real y_length = 0.0;
    amrex::Real z_length = 0.0;
    amrex::Real dx = 0.0;
    amrex::Real dt = 0.0;
    amrex::Real dx_min = 0.0;
    amrex::Real dt_min = 0.0;
};

#endif
