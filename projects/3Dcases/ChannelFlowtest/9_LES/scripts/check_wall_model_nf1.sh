#!/bin/bash
set -euo pipefail

case_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
amr_cpp="${case_dir}/src/AmrCoreLBM.cpp"
kernels="${case_dir}/src/Kernels.H"

rg -q "particles\\[i\\]->InterpForceWallModel\\(lev, rho_lev, u_lev, force_lev\\);" "${amr_cpp}"
rg -q "compute_u_tau_werner_wengle" "${kernels}"
rg -q "compute_uD_apgpl" "${kernels}"
rg -q "ibm_interp_rho_at" "${kernels}"
rg -q "const amrex::Real nx = 0.0;" "${kernels}"
rg -q "const amrex::Real ny = is_bottom_wall \\? 1.0 : -1.0;" "${kernels}"
rg -q "amrex::Real UBRx = UBx;" "${kernels}"
rg -q "rhot \\* \\(uLagx - UBRx\\)" "${kernels}"

if rg -q "xq =|xRef =|fTang|tw =|normal = \\{x_local / distSum" "${kernels}"; then
    echo "old direct-shear wall-model logic is still present" >&2
    exit 1
fi
