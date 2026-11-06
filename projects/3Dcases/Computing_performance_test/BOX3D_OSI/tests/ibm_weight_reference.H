// 优化前直接核求值路径，仅用于逐值回归，禁止用于生产推进。
#if AMREX_SPACEDIM == 2
template <typename P>
AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE void reference_force_interp_extrap(
    P const& p, ParticleReal& fx, ParticleReal& fy, ParticleReal& tz,
    ParticleReal& x_local, ParticleReal& y_local, ParticleReal& arc,
    Array4<Real> const& u, Array4<Real> const& rho, Array4<Real> const& force,
    Real delta, RealVect const& target, LbmGridParams const& grid) {
    // 二维只访问 z=0；arc 是格子弧长，权重乘 dx_min² 给出单位厚度的力。
    const Real xt = p.pos(0) / delta, yt = p.pos(1) / delta;
    Real ux = 0, uy = 0, density = 0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            const int i = int(amrex::Math::floor(xt)) + x;
            const int j = int(amrex::Math::floor(yt)) + y;
            const Real w = delta3p(xt - (i + Real(0.5))) * delta3p(yt - (j + Real(0.5)));
            ux += w * u(i,j,0,0);
            uy += w * u(i,j,0,1);
            density += w * rho(i,j,0,0);
        }
    }
    const Real fxt = 2 * density * (ux - target[0]) / grid.dt_min;
    const Real fyt = 2 * density * (uy - target[1]) / grid.dt_min;
    const Real weight = LP_dr * arc;
    const Real volume = grid.dx_min * grid.dx_min;
    fx = fxt * weight * volume;
    fy = fyt * weight * volume;
    tz = (fyt * x_local - fxt * y_local) * weight * volume;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            const int i = int(amrex::Math::floor(xt)) + x;
            const int j = int(amrex::Math::floor(yt)) + y;
            const Real w = delta3p(xt - (i + Real(0.5))) * delta3p(yt - (j + Real(0.5))) * weight;
            amrex::Gpu::Atomic::AddNoRet(&force(i,j,0,0), -fxt*w);
            amrex::Gpu::Atomic::AddNoRet(&force(i,j,0,1), -fyt*w);
        }
    }
}
#else
template <typename P>
AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE void reference_force_interp_extrap(
    P const& p, amrex::ParticleReal& fx, amrex::ParticleReal& fy, amrex::ParticleReal& fz,
    amrex::ParticleReal& tx, amrex::ParticleReal& ty, amrex::ParticleReal& tz, amrex::ParticleReal& x_local,
    amrex::ParticleReal& y_local, amrex::ParticleReal& z_local, amrex::ParticleReal& area,
    amrex::Array4<amrex::Real> const& u, amrex::Array4<amrex::Real> const& rho,
    amrex::Array4<amrex::Real> const& Ft, amrex::Real const delta,
    amrex::RealVect const& uc, amrex::RealVect const& wc,
    amrex::RealVect const& centre, LbmGridParams const& grid) {
    amrex::Real xt, yt, zt, lx, ly, lz;
    amrex::Real IB_Interp, IB_weight;
    amrex::Real uxt, uyt, uzt, rhot, upx, upy, upz, fxt, fyt, fzt;

    xt = p.pos(0) / delta;
    yt = p.pos(1) / delta;
    zt = p.pos(2) / delta;

    IB_weight = LP_dr * area;

    uxt = 0.0_rt;
    uyt = 0.0_rt;
    uzt = 0.0_rt;
    rhot = 0.0_rt;

    upx = uc[0] + wc[2] * (-y_local) + wc[1] * (z_local);
    upy = uc[1] + wc[2] * (x_local) + wc[0] * (-z_local);
    upz = uc[2] + wc[0] * (y_local) + wc[1] * (-x_local);

    for (int x = -2; x <= 2; x++) {
        for (int y = -2; y <= 2; y++) {
            for (int z = -2; z <= 2; z++) {
                int xx = static_cast<int>(amrex::Math::floor(xt)) +
                         x; // 拉格朗日点作用范围内真实的点坐标
                int yy = static_cast<int>(amrex::Math::floor(yt)) + y;
                int zz = static_cast<int>(amrex::Math::floor(zt)) + z;

                lx = xt - (xx + 0.5_rt);
                ly = yt - (yy + 0.5_rt);
                lz = zt - (zz + 0.5_rt);

                IB_Interp = delta3p(lx) * delta3p(ly) * delta3p(lz);

                uxt += u(xx, yy, zz, 0) * IB_Interp;
                uyt += u(xx, yy, zz, 1) * IB_Interp;
                uzt += u(xx, yy, zz, 2) * IB_Interp;
                rhot += rho(xx, yy, zz, 0) * IB_Interp;
            }
        }
    }

    rhot = 2 * rhot / grid.dt_min;

    fxt = rhot * (uxt - upx);
    fyt = rhot * (uyt - upy);
    fzt = rhot * (uzt - upz);

    fx = fxt * IB_weight * grid.dx_min * grid.dx_min * grid.dx_min;
    fy = fyt * IB_weight * grid.dx_min * grid.dx_min * grid.dx_min;
    fz = fzt * IB_weight * grid.dx_min * grid.dx_min * grid.dx_min;

    tx = (fzt * y_local - fyt * z_local) * IB_weight * grid.dx_min * grid.dx_min * grid.dx_min;
    ty = (fxt * z_local - fzt * x_local) * IB_weight * grid.dx_min * grid.dx_min * grid.dx_min;
    tz = (fyt * x_local - fxt * y_local) * IB_weight * grid.dx_min * grid.dx_min * grid.dx_min;

    for (int x = -2; x <= 2; x++) {
        for (int y = -2; y <= 2; y++) {
            for (int z = -2; z <= 2; z++) {
                int xx = static_cast<int>(amrex::Math::floor(xt)) + x;
                int yy = static_cast<int>(amrex::Math::floor(yt)) + y;
                int zz = static_cast<int>(amrex::Math::floor(zt)) + z;

                lx = xt - (xx + 0.5_rt);
                ly = yt - (yy + 0.5_rt);
                lz = zt - (zz + 0.5_rt);

                IB_Interp = delta3p(lx) * delta3p(ly) * delta3p(lz) * IB_weight;

                amrex::Gpu::Atomic::AddNoRet(&Ft(xx, yy, zz, 0), -fxt * IB_Interp);
                amrex::Gpu::Atomic::AddNoRet(&Ft(xx, yy, zz, 1), -fyt * IB_Interp);
                amrex::Gpu::Atomic::AddNoRet(&Ft(xx, yy, zz, 2), -fzt * IB_Interp);
            }
        }
    }
}

#endif
