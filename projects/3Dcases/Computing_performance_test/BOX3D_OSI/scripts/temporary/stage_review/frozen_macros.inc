// 从长跑冻结Kernels.H精确提取两个函数，保留原算术顺序。
AMREX_GPU_DEVICE AMREX_FORCE_INLINE void compute_macro(int i, int j, int k, amrex::Array4<amrex::Real> const& fold,
                   amrex::Array4<amrex::Real> const& rho,
                   amrex::Array4<amrex::Real> const& u) {
    Real uxt = 0.0_rt, uyt = 0.0_rt, uzt = 0.0_rt, rhot = 0.0_rt, ft = 0.0_rt;

    for (int q = 0; q < Q; q++) {
        ft = fold(i, j, k, q);
        rhot += ft;
        uxt += ft * e[q][0];
        uyt += ft * e[q][1];
        uzt += ft * lattice_ez(q);
    }

    u(i, j, k, 0) = uxt / rhot;
    u(i, j, k, 1) = uyt / rhot;
#if AMREX_SPACEDIM == 3
    u(i, j, k, 2) = uzt / rhot;
#endif
    rho(i, j, k, 0) = rhot;
}
AMREX_GPU_DEVICE AMREX_FORCE_INLINE void compute_macro_osi(
    int i, int j, int k, amrex::Array4<const amrex::Real> const& state,
    amrex::Array4<amrex::Real> const& rho,
    amrex::Array4<amrex::Real> const& u, OSI::FabGeometry fab,
    OSI::PhaseShift3 phase_shift) noexcept {
    Real rhot = 0.0_rt;
    Real uxt = 0.0_rt;
    Real uyt = 0.0_rt;
    Real uzt = 0.0_rt;

#pragma unroll
    for (int q = 0; q < Q; ++q) {
        const auto raw = OSI::osi_address(
            {i, j, k}, {e[q][0], e[q][1], lattice_ez(q)}, fab, phase_shift);
        const Real ft = state(raw.x, raw.y, raw.z, q);
        rhot += ft;
        uxt += ft * e[q][0];
        uyt += ft * e[q][1];
        uzt += ft * lattice_ez(q);
    }

    const Real inv_rho = 1.0_rt / rhot;
    u(i, j, k, 0) = uxt * inv_rho;
    u(i, j, k, 1) = uyt * inv_rho;
#if AMREX_SPACEDIM == 3
    u(i, j, k, 2) = uzt * inv_rho;
#endif
    rho(i, j, k, 0) = rhot;
}
