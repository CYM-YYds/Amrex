#include "LagrangeParticleContainer.H"
#include "LbmLattice.H"
#include "Kernels.H"
using namespace amrex;

void LagrangeParticleContainer::PrintParticleParm() {
#if AMREX_SPACEDIM == 2
    amrex::Print() << "Particle geometry: disk radius=" << radius * grid_.dx_min
                   << " mass_per_depth=" << particle_mass_
                   << " inertia_per_depth=" << particle_inertia_ << "\n";
#else
    amrex::Print() << "╔══════════════════════════════════════════════════════╗" << std::endl;
    amrex::Print() << "║                      IBM PARAMETERS                  ║" << std::endl;
    amrex::Print() << "╚══════════════════════════════════════════════════════╝" << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  R      =" << std::setw(10) << std::right << R << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  rb     =" << std::setw(10) << std::right << rb << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  ds0    =" << std::setw(10) << std::right << ds0 << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  ns     =" << std::setw(10) << std::right << ns << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  nt1    =" << std::setw(10) << std::right << nt1 << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  LP_area=" << std::setw(10) << std::right << LP_area << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  LP_dr  =" << std::setw(10) << std::right << LP_dr << std::endl;

    amrex::Print() << std::setw(15) << std::left << "  rhof   =" << std::setw(10) << std::right << rhof << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  rhop   =" << std::setw(10) << std::right << rhop << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  mp     =" << std::setw(10) << std::right << particle_mass_ << std::endl;
    // amrex::Print() << std::setw(15) << std::left << "  mf     =" << std::setw(10) << std::right << Mf << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  Mp     =" << std::setw(10) << std::right << particle_inertia_ << std::endl;
    // amrex::Print() << std::setw(15) << std::left << "  Mf     =" << std::setw(10) << std::right << Mf_iner << std::endl;

    amrex::Print() << "╚══════════════════════════════════════════════════════╝" << std::endl;
    amrex::Print() << std::endl;
#endif
}

void LagrangeParticleContainer::InitParticle(int lev) {
#if AMREX_SPACEDIM == 2
    // 二维仅生成圆盘边界；area 槽保存以最细网格为单位的弧长。
    const int count = static_cast<int>(std::ceil(2 * PI * radius / ds0));
    if (ParallelDescriptor::IOProcessor()) {
        auto& tile = GetParticles(0)[{0, 0}];
        for (int n = 0; n < count; ++n) {
            const Real angle = 2 * PI * n / count;
            ParticleType p;
            p.id() = ParticleType::NextID();
            p.cpu() = ParallelDescriptor::MyProc();
            std::array<ParticleReal, PIdx::nattribs> attribs{};
            attribs[PIdx::xlocal] = radius * std::cos(angle) * grid_.dx_min;
            attribs[PIdx::ylocal] = radius * std::sin(angle) * grid_.dx_min;
            attribs[PIdx::area] = 2 * PI * radius / count;
            for (int d = 0; d < AMREX_SPACEDIM; ++d) {
                p.pos(d) = Geom(0).ProbLo(d) + centre[d] * Geom(0).CellSize(d) + attribs[PIdx::xlocal + d];
                AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
                    p.pos(d) > Geom(0).ProbLo(d) && p.pos(d) < Geom(0).ProbHi(d),
                    "disk boundary must lie strictly inside the physical domain");
            }
            tile.push_back(p);
            tile.push_back_real(attribs);
        }
    }
    Redistribute();
#else
    ParticleType p;
    std::array<ParticleReal, PIdx::nattribs> attribs;
    const Real* delta = Geom(0).CellSize();

    Real sita = 0.0, alpha = 0.0;
    int nt = 1;

    int num_proc = ParallelDescriptor::NProcs();

    if (ParallelDescriptor::MyProc() == ParallelDescriptor::IOProcessorNumber()) {
        for (int i = 0; i < ns; i++) {
            if (i == 0) {
                p.id() = ParticleType::NextID();
                p.cpu() = ParallelDescriptor::MyProc(); // int(p.id()%num_proc);
                p.pos(0) = (centre[0]) * delta[0];      // TODO:修改为局部坐标系失败
                p.pos(1) = (centre[1]) * delta[0];
                p.pos(2) = (centre[2]) * delta[0] + radius * grid_.dx_min;

                attribs[PIdx::xlocal] = 0.0;
                attribs[PIdx::ylocal] = 0.0;
                attribs[PIdx::zlocal] = radius * grid_.dx_min;

                attribs[PIdx::area] = LP_area * (1.0 - cos(PI / (2 * (ns - 1)))) / 2.0;
                attribs[PIdx::fx] = 0.0;
                attribs[PIdx::fy] = 0.0;
                attribs[PIdx::fz] = 0.0;
                attribs[PIdx::tx] = 0.0;
                attribs[PIdx::ty] = 0.0;
                attribs[PIdx::tz] = 0.0;

                std::pair<int, int> key{0, 0};
                auto& particle_tile = GetParticles(0)[key];

                particle_tile.push_back(p);
                particle_tile.push_back_real(attribs);
            } else if (i > 0 && i < ns - 1) {
                sita = i * PI / (ns - 1);
                nt = int(nt1 * sin(sita));

                for (int j = 0; j < nt; j++) {
                    alpha = 2 * PI / nt * (j + 1);

                    p.id() = ParticleType::NextID();
                    p.cpu() = ParallelDescriptor::MyProc(); // int(p.id()/num_proc);

                    p.pos(0) = (centre[0]) * delta[0] + radius * sin(sita) * cos(alpha) * grid_.dx_min;
                    p.pos(1) = (centre[1]) * delta[0] + radius * sin(sita) * sin(alpha) * grid_.dx_min;
                    p.pos(2) = (centre[2]) * delta[0] + radius * cos(sita) * grid_.dx_min;

                    attribs[PIdx::xlocal] = radius * sin(sita) * cos(alpha) * grid_.dx_min;
                    attribs[PIdx::ylocal] = radius * sin(sita) * sin(alpha) * grid_.dx_min;
                    attribs[PIdx::zlocal] = radius * cos(sita) * grid_.dx_min;
                    attribs[PIdx::area] = LP_area * (cos((i - 0.5) * PI / (ns - 1)) - cos((i + 0.5) * PI / (ns - 1))) / 2.0 / nt;
                    attribs[PIdx::fx] = 0.0;
                    attribs[PIdx::fy] = 0.0;
                    attribs[PIdx::fz] = 0.0;
                    attribs[PIdx::tx] = 0.0;
                    attribs[PIdx::ty] = 0.0;
                    attribs[PIdx::tz] = 0.0;

                    std::pair<int, int> key{0, 0};
                    auto& particle_tile = GetParticles(0)[key];

                    particle_tile.push_back(p);
                    particle_tile.push_back_real(attribs);
                }
            } else {
                p.id() = ParticleType::NextID();
                p.cpu() = ParallelDescriptor::MyProc(); // int(p.id()/num_proc);
                p.pos(0) = (centre[0]) * delta[0];
                p.pos(1) = (centre[1]) * delta[0];
                p.pos(2) = (centre[2]) * delta[0] - radius * grid_.dx_min;

                attribs[PIdx::xlocal] = 0.0;
                attribs[PIdx::ylocal] = 0.0;
                attribs[PIdx::zlocal] = -radius * grid_.dx_min;
                attribs[PIdx::area] = LP_area * (1.0 - cos(PI / (2 * (ns - 1)))) / 2.0;
                attribs[PIdx::fx] = 0.0;
                attribs[PIdx::fy] = 0.0;
                attribs[PIdx::fz] = 0.0;
                attribs[PIdx::tx] = 0.0;
                attribs[PIdx::ty] = 0.0;
                attribs[PIdx::tz] = 0.0;

                std::pair<int, int> key{0, 0};
                auto& particle_tile = GetParticles(0)[key];

                particle_tile.push_back(p);
                particle_tile.push_back_real(attribs);
            }
        }
    }
    Redistribute();
#endif
}

void LagrangeParticleContainer::InitParticleFromFile(int lev, const std::string object) {
#if AMREX_SPACEDIM == 2
    amrex::Abort("DIM=2 supports generated disks and disk checkpoints only; 3D .lpa files are forbidden");
#else
    ParticleType p;
    std::array<ParticleReal, PIdx::nattribs> attribs;
    const Real* delta = Geom(0).CellSize();

    int num_proc = ParallelDescriptor::NProcs();
    int num_points;
    amrex::Real factor = 1.0;

    if (ParallelDescriptor::MyProc() == ParallelDescriptor::IOProcessorNumber()) {
        FILE* fp;

        std::string file = object + ".lpa";

        if ((fp = fopen(file.c_str(), "rb")) == NULL) {
            amrex::Print() << "error in read stl!" << std::endl;
        } else {
            printf("file is opened!\n");
            fscanf(fp, "%d", &num_points);

            for (size_t j = 0; j < num_points; j++) {
                amrex::Real pos_x, pos_y, pos_z, area_tmp;
                amrex::Real dir_x, dir_y, dir_z;

                fscanf(fp, "%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\n",
                       &pos_x, &pos_y, &pos_z, &area_tmp, &dir_x, &dir_x, &dir_x);

                p.id() = ParticleType::NextID();
                p.cpu() = ParallelDescriptor::MyProc();
                p.pos(0) = (centre[0]) * delta[0] + pos_x / factor * grid_.dx_min;
                p.pos(1) = (centre[1]) * delta[0] + pos_y / factor * grid_.dx_min;
                p.pos(2) = (centre[2]) * delta[0] + pos_z / factor * grid_.dx_min;

                attribs[PIdx::xlocal] = 0.0; // 这个可以先设置成为0，目前不需要计算转矩等参数
                attribs[PIdx::ylocal] = 0.0;
                attribs[PIdx::zlocal] = 0.0;
                attribs[PIdx::area] = area_tmp / factor / factor;

                attribs[PIdx::fx] = 0.0;
                attribs[PIdx::fy] = 0.0;
                attribs[PIdx::fz] = 0.0;
                attribs[PIdx::tx] = 0.0;
                attribs[PIdx::ty] = 0.0;
                attribs[PIdx::tz] = 0.0;

                std::pair<int, int> key{0, 0};
                auto& particle_tile = GetParticles(0)[key];

                particle_tile.push_back(p);
                particle_tile.push_back_real(attribs);
            }
        }
        fclose(fp);
    }

    Redistribute();
#endif
}

#if AMREX_SPACEDIM == 3
void LagrangeParticleContainer::MoveParticle(int lev, amrex::Real cur_time) {
    amrex::RealVect pos_dif, pos_new;
    amrex::RealVect vel_new, angvel_new;
    amrex::RealVect G_dire{0.0, 0.0, -1.0};
    amrex::Real Fgra = (1.0 - rhof / rhop) * particle_mass_ * G;

    for (int i = 0; i < AMREX_SPACEDIM; i++) {
        // F_lub[i] = 0.0;

        vel_new[i] = (1.0 + rhof / rhop) * vel[i] - (rhof / rhop) * vel_old[i] + (F_tot[i] + Fgra * G_dire[i] + F_lub[i]) / particle_mass_ * grid_.dt_min;
        pos_new[i] = centre[i] + grid_.dt_min * 0.5 * (vel_new[i] + vel[i]) / grid_.dx;
        vel_old[i] = vel[i];
        vel[i] = vel_new[i];

        angvel_new[i] = (1.0 + rhof / rhop) * angvel[i] - (rhof / rhop) * angvel_old[i] + T_tot[i] * grid_.dt_min / particle_inertia_;
        angvel_old[i] = angvel[i];
        angvel[i] = angvel_new[i];
    }

    // 更新拉格朗日点绝对位置
    pos_dif[0] = (pos_new[0] - centre[0]) * grid_.dx;
    pos_dif[1] = (pos_new[1] - centre[1]) * grid_.dx;
    pos_dif[2] = (pos_new[2] - centre[2]) * grid_.dx;

    // amrex::Print() << "dif_x = " << pos_dif[0] << ", " << "dif_y = " << pos_dif[1] << ", " << "dif_z = " << pos_dif[2] << std::endl;

    centre[0] = pos_new[0];
    centre[1] = pos_new[1];
    centre[2] = pos_new[2];

    const Real delta = Geom(lev).CellSize()[0];
    const LbmGridParams grid = grid_;

    for (MyParIter pti(*this, lev); pti.isValid(); ++pti) {
        auto& particles = pti.GetArrayOfStructs();
        auto p_ptr = particles().data();
        const long n = pti.numParticles();

        auto& attribs = pti.GetAttribs();
        auto xlocal = attribs[PIdx::xlocal].data();
        auto ylocal = attribs[PIdx::ylocal].data();
        auto zlocal = attribs[PIdx::zlocal].data();

        const RealVect& centre_pos = centre;

        amrex::ParallelFor(n, [=] AMREX_GPU_DEVICE(int i) noexcept {
            p_ptr[i].pos(0) = centre_pos[0] * grid.dx + xlocal[i];
            p_ptr[i].pos(1) = centre_pos[1] * grid.dx + ylocal[i];
            p_ptr[i].pos(2) = centre_pos[2] * grid.dx + zlocal[i];
        });
    }
}
#endif

amrex::RealVect LagrangeParticleContainer::ReturnCentre() {
    return centre;
}

amrex::RealVect LagrangeParticleContainer::ReturnVelocity() {
    return vel;
}

#if AMREX_SPACEDIM == 3
void LagrangeParticleContainer::InterpForce(int lev, amrex::MultiFab& rho_lev, amrex::MultiFab& u_lev, amrex::MultiFab& force_lev) {
    const Real delta = Geom(lev).CellSize()[0];
    const LbmGridParams grid = grid_;

    for (MyParIter pti(*this, lev); pti.isValid(); ++pti) {
        auto& particles = pti.GetArrayOfStructs();
        auto p_ptr = particles().data();
        const long n = pti.numParticles();

        auto& attribs = pti.GetAttribs();
        auto fx = attribs[PIdx::fx].data();
        auto fy = attribs[PIdx::fy].data();
        auto fz = attribs[PIdx::fz].data();
        auto tx = attribs[PIdx::tx].data();
        auto ty = attribs[PIdx::ty].data();
        auto tz = attribs[PIdx::tz].data();
        auto xlocal = attribs[PIdx::xlocal].data();
        auto ylocal = attribs[PIdx::ylocal].data();
        auto zlocal = attribs[PIdx::zlocal].data();
        auto area = attribs[PIdx::area].data();

        const Array4<Real>& u = u_lev.array(pti);
        const Array4<Real>& rho = rho_lev.array(pti);
        const Array4<Real>& Ft = force_lev.array(pti);

        const RealVect& uc = vel;
        const RealVect& wc = angvel;
        const RealVect& pos = centre;

        amrex::ParallelFor(n, [=] AMREX_GPU_DEVICE(int i) noexcept {
            force_interp_extrap(p_ptr[i], fx[i], fy[i], fz[i], tx[i], ty[i], tz[i], xlocal[i], ylocal[i], zlocal[i], area[i],
                                u, rho, Ft, delta, uc, wc, pos, grid);
        });
    }

    // 统计转矩等颗粒受力
    using SPType = typename LagrangeParticleContainer::SuperParticleType;

    auto fx = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
        return p.rdata(PIdx::fx);
    });

    auto fy = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
        return p.rdata(PIdx::fy);
    });

    auto fz = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
        return p.rdata(PIdx::fz);
    });

    auto tx = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
        return p.rdata(PIdx::tx);
    });

    auto ty = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
        return p.rdata(PIdx::ty);
    });
    auto tz = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
        return p.rdata(PIdx::tz);
    });

    ParallelDescriptor::ReduceRealSum(fx);
    ParallelDescriptor::ReduceRealSum(fy);
    ParallelDescriptor::ReduceRealSum(fz);
    ParallelDescriptor::ReduceRealSum(tx);
    ParallelDescriptor::ReduceRealSum(ty);
    ParallelDescriptor::ReduceRealSum(tz);

    F_tot[0] = fx;
    F_tot[1] = fy;
    F_tot[2] = fz;
    T_tot[0] = tx;
    T_tot[1] = ty;
    T_tot[2] = tz;
    F_lub[0] = 0.0;
    F_lub[1] = 0.0;
    F_lub[2] = 0.0;
}
#endif

#if AMREX_SPACEDIM == 3
void LagrangeParticleContainer::InterpForceWallModel(int lev, amrex::MultiFab& rho_lev, amrex::MultiFab& u_lev, amrex::MultiFab& force_lev) {
    const Real delta = Geom(lev).CellSize()[0];
    const LbmGridParams grid = grid_;

    for (MyParIter pti(*this, lev); pti.isValid(); ++pti) {
        auto& particles = pti.GetArrayOfStructs();
        auto p_ptr = particles().data();
        const long n = pti.numParticles();

        auto& attribs = pti.GetAttribs();
        auto fx = attribs[PIdx::fx].data();
        auto fy = attribs[PIdx::fy].data();
        auto fz = attribs[PIdx::fz].data();
        auto tx = attribs[PIdx::tx].data();
        auto ty = attribs[PIdx::ty].data();
        auto tz = attribs[PIdx::tz].data();
        auto xlocal = attribs[PIdx::xlocal].data();
        auto ylocal = attribs[PIdx::ylocal].data();
        auto zlocal = attribs[PIdx::zlocal].data();
        auto area = attribs[PIdx::area].data();

        const Array4<Real>& u = u_lev.array(pti);
        const Array4<Real>& rho = rho_lev.array(pti);
        const Array4<Real>& Ft = force_lev.array(pti);

        const RealVect& uc = vel;
        const RealVect& wc = angvel;
        const RealVect& pos = centre;

        amrex::ParallelFor(n, [=] AMREX_GPU_DEVICE(int i) noexcept {
            force_wall_model(p_ptr[i], fx[i], fy[i], fz[i], tx[i], ty[i], tz[i], xlocal[i], ylocal[i], zlocal[i], area[i],
                             u, rho, Ft, delta, uc, wc, pos, grid);
        });
    }

    // 统计转矩等颗粒受力
    using SPType = typename LagrangeParticleContainer::SuperParticleType;

    auto fx = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
        return p.rdata(PIdx::fx);
    });

    auto fy = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
        return p.rdata(PIdx::fy);
    });

    auto fz = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
        return p.rdata(PIdx::fz);
    });

    auto tx = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
        return p.rdata(PIdx::tx);
    });

    auto ty = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
        return p.rdata(PIdx::ty);
    });
    auto tz = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
        return p.rdata(PIdx::tz);
    });

    ParallelDescriptor::ReduceRealSum(fx);
    ParallelDescriptor::ReduceRealSum(fy);
    ParallelDescriptor::ReduceRealSum(fz);
    ParallelDescriptor::ReduceRealSum(tx);
    ParallelDescriptor::ReduceRealSum(ty);
    ParallelDescriptor::ReduceRealSum(tz);

    F_tot[0] = fx;
    F_tot[1] = fy;
    F_tot[2] = fz;
    T_tot[0] = tx;
    T_tot[1] = ty;
    T_tot[2] = tz;
    F_lub[0] = 0.0;
    F_lub[1] = 0.0;
    F_lub[2] = 0.0;
}
#endif

#if AMREX_SPACEDIM == 3
void LagrangeParticleContainer::CollideParticle(const std::unique_ptr<LagrangeParticleContainer>& p2) {
    amrex::RealVect Fpw_lub = {0.0, 0.0, 0.0};
    amrex::RealVect Fp1_lub = {0.0, 0.0, 0.0};
    amrex::RealVect Fp2_lub = {0.0, 0.0, 0.0};

    amrex::RealVect pos_p1 = this->ReturnCentre();
    amrex::RealVect pos_p2 = p2->ReturnCentre();
    amrex::RealVect vel_p1 = this->ReturnVelocity();
    amrex::RealVect vel_p2 = p2->ReturnVelocity();

    amrex::Real lxc = pos_p1[0] - pos_p2[0]; // 位置都是lev = 0的网格下
    amrex::Real lyc = pos_p1[1] - pos_p2[1];
    amrex::Real lzc = pos_p1[2] - pos_p2[2];
    amrex::Real lc = sqrt(lxc * lxc + lyc * lyc + lzc * lzc);

    amrex::Real Ftmp = 0.0;
    amrex::Real Fgra = (1.0 - 1.0 * rhof / rhop) * particle_mass_ * G * grid_.dx_min; // 调整了大小不要忘记
    amrex::Real npp1 = (lc - R * 2.0 - safe) / safe;
    amrex::Real npp2 = (R * 2.0 - lc) / safe;

    if (lc <= (2 * R)) {
        Ftmp = std::fabs(Fgra / Epp1) * npp1 * npp1 + std::fabs(Fgra / Epp2) * npp2;
    } else if (lc <= (2 * R + safe)) {
        Ftmp = std::fabs(Fgra / Epp1) * npp1 * npp1;
    } else {
        Ftmp = 0.0;
    }

    Fp1_lub[0] += Ftmp * lxc / lc;
    Fp1_lub[1] += Ftmp * lyc / lc;
    Fp1_lub[2] += Ftmp * lzc / lc;

    Fp2_lub[0] -= Ftmp * lxc / lc;
    Fp2_lub[1] -= Ftmp * lyc / lc;
    Fp2_lub[2] -= Ftmp * lzc / lc;

    F_lub[0] = Fp1_lub[0];
    F_lub[1] = Fp1_lub[1];
    F_lub[2] = Fp1_lub[2];

    p2->SetLubVal(Fp2_lub); // 得有一个单独清零的地方
}
#endif

#if AMREX_SPACEDIM == 3
void LagrangeParticleContainer::CollideWall() {
    amrex::RealVect Fpw_lub = {0.0, 0.0, 0.0};

    amrex::RealVect pos_p1 = this->ReturnCentre();
    amrex::RealVect vel_p1 = this->ReturnVelocity();

    amrex::Real lxc, lyc, lzc, lc;
    amrex::Real Ftmp, npw1, npw2, xc_fict;

    amrex::Real Fgra = (1.0 - rhof / rhop) * particle_mass_ * G * grid_.dx_min;

    // 左边界
    if (pos_p1[0] <= (R + safe + 1.0)) {
        xc_fict = 1.0 - R;
        lxc = pos_p1[0] - xc_fict;
        lc = std::abs(lxc);

        npw1 = (lc - R * 2.0 - safe) / safe;
        npw2 = (R * 2.0 - lc) / safe;

        if (lc <= (2 * R)) {
            Ftmp = std::fabs(Fgra / Epw1) * npw1 * npw1 + std::fabs(Fgra / Epw2) * npw2;
        } else if (lc <= (2 * R + safe)) {
            Ftmp = std::fabs(Fgra / Epw1) * npw1 * npw1;
        } else {
            Ftmp = 0.0;
        }

        Fpw_lub[0] += Ftmp * lxc / lc;
    }
    // 右边界
    if (pos_p1[0] >= (grid_.nx_cells - R - safe - 1.0)) {
        xc_fict = grid_.nx_cells + R - 1.0;
        lxc = pos_p1[0] - xc_fict;
        lc = std::abs(lxc);

        npw1 = (lc - R * 2.0 - safe) / safe;
        npw2 = (R * 2.0 - lc) / safe;

        if (lc <= (2 * R)) {
            Ftmp = std::fabs(Fgra / Epw1) * npw1 * npw1 + std::fabs(Fgra / Epw2) * npw2;
        } else if (lc <= (2 * R + safe)) {
            Ftmp = std::fabs(Fgra / Epw1) * npw1 * npw1;
        } else {
            Ftmp = 0.0;
        }

        Fpw_lub[0] += Ftmp * lxc / lc;
    }

    // 后边界
    if (pos_p1[1] <= (R + safe + 1.0)) {
        xc_fict = 1.0 - R; // 这里到底是多少
        lxc = pos_p1[1] - xc_fict;
        lc = std::abs(lxc);

        npw1 = (lc - R * 2.0 - safe) / safe;
        npw2 = (R * 2.0 - lc) / safe;

        if (lc <= (2 * R)) {
            Ftmp = std::fabs(Fgra / Epw1) * npw1 * npw1 + std::fabs(Fgra / Epw2) * npw2;
        } else if (lc <= (2 * R + safe)) {
            Ftmp = std::fabs(Fgra / Epw1) * npw1 * npw1;
        } else {
            Ftmp = 0.0;
        }

        Fpw_lub[1] += Ftmp * lxc / lc;
    }

    // 前边界
    if (pos_p1[1] >= (grid_.ny_cells - R - safe - 1.0)) {
        xc_fict = grid_.ny_cells + R - 1.0; // 这里到底是多少
        lxc = pos_p1[1] - xc_fict;
        lc = std::abs(lxc);

        npw1 = (lc - R * 2.0 - safe) / safe;
        npw2 = (R * 2.0 - lc) / safe;

        if (lc <= (2 * R)) {
            Ftmp = std::fabs(Fgra / Epw1) * npw1 * npw1 + std::fabs(Fgra / Epw2) * npw2;
        } else if (lc <= (2 * R + safe)) {
            Ftmp = std::fabs(Fgra / Epw1) * npw1 * npw1;
        } else {
            Ftmp = 0.0;
        }

        Fpw_lub[1] += Ftmp * lxc / lc;
    }

    // 下边界
    if (pos_p1[2] <= (R + safe + 1.0)) {
        xc_fict = 1.0 - R; // 这里到底是多少
        lxc = pos_p1[2] - xc_fict;
        lc = std::abs(lxc);

        npw1 = (lc - R * 2.0 - safe) / safe;
        npw2 = (R * 2.0 - lc) / safe;

        if (lc <= (2 * R)) {
            Ftmp = std::fabs(Fgra / Epw1) * npw1 * npw1 + std::fabs(Fgra / Epw2) * npw2;
        } else if (lc <= (2 * R + safe)) {
            Ftmp = std::fabs(Fgra / Epw1) * npw1 * npw1;
        } else {
            Ftmp = 0.0;
        }

        Fpw_lub[2] += Ftmp * lxc / lc;
    }

    // 上边界
    if (pos_p1[2] >= (grid_.nz_cells - R - safe - 1.0)) {
        xc_fict = grid_.nz_cells + R - 1.0; // 这里到底是多少
        lxc = pos_p1[2] - xc_fict;
        lc = std::abs(lxc);

        npw1 = (lc - R * 2.0 - safe) / safe;
        npw2 = (R * 2.0 - lc) / safe;

        if (lc <= (2 * R)) {
            Ftmp = std::fabs(Fgra / Epw1) * npw1 * npw1 + std::fabs(Fgra / Epw2) * npw2;
        } else if (lc <= (2 * R + safe)) {
            Ftmp = std::fabs(Fgra / Epw1) * npw1 * npw1;
        } else {
            Ftmp = 0.0;
        }

        Fpw_lub[2] += Ftmp * lxc / lc;
    }

    F_lub[0] += Fpw_lub[0];
    F_lub[1] += Fpw_lub[1];
    F_lub[2] += Fpw_lub[2];
}
#endif

#if AMREX_SPACEDIM == 3
void LagrangeParticleContainer::SetLubVal(amrex::RealVect& force_lub) {
    F_lub[0] += force_lub[0];
    F_lub[1] += force_lub[1];
    F_lub[2] += force_lub[2];
}
#endif

void LagrangeParticleContainer::SaveVelocity(int lev, int step) {
    amrex::Real uz = vel[AMREX_D_PICK(0, 1, 2)] * U0;

    if (ParallelDescriptor::MyProc() == ParallelDescriptor::IOProcessorNumber()) {
        std::string filename = "vel_" + std::to_string(id) + ".dat";
        std::ofstream file(filename, std::ios::app);

        if (!file.is_open()) {
            std::cerr << "Cannot open the file: " << filename << std::endl;
            std::exit(1); // 错误退出
        }

        file << step << "\t" << uz << "\n";
        file.close();
    }
}

void LagrangeParticleContainer::SavePosition(int lev, int step) {
    amrex::Real pos_x = centre[0];
    amrex::Real pos_y = centre[1];
    amrex::Real pos_z = AMREX_D_PICK(0, 0, centre[2]);

    if (ParallelDescriptor::MyProc() == ParallelDescriptor::IOProcessorNumber()) {
        std::string filename = "pos_" + std::to_string(id) + ".dat";
        std::ofstream file(filename, std::ios::app);

        if (!file.is_open()) {
            std::cerr << "Cannot open the file: " << filename << std::endl;
            std::exit(1); // 错误退出
        }

        file << step << "\t" << pos_x << "\t" << pos_y;
#if AMREX_SPACEDIM == 3
        file << "\t" << pos_z;
#endif
        file << "\n";
        file.close();
    }
}

#if AMREX_SPACEDIM == 3
void LagrangeParticleContainer::SaveFxy(int lev, int step) {
    using SPType = typename LagrangeParticleContainer::SuperParticleType;

    auto fx = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
        return p.rdata(PIdx::fx);
    });

    auto fy = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
        return p.rdata(PIdx::fy);
    });

    auto fz = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
        return p.rdata(PIdx::fz);
    });

    ParallelDescriptor::ReduceRealSum(fx);
    ParallelDescriptor::ReduceRealSum(fy);
    ParallelDescriptor::ReduceRealSum(fz);

    amrex::Real m = 0.5 * (rho0)*U0 * U0 * PI * D * grid_.dx * D * grid_.dx / 4.0;
    fx /= m;
    fy /= m;
    fz /= m;

    // if(ParallelDescriptor::MyProc() == ParallelDescriptor::IOProcessorNumber())
    // {
    //     FILE* file;

    //     if((file = fopen("CdCl.dat", "a")) == NULL)
    //     {
    //         printf("can not open the file!");
    //         exit(0);
    //     }
    //     fprintf(file, "%d\t%f\t%f\t%f\n", step, fx, fy, fz);
    //     fclose(file);
    // }

    if (ParallelDescriptor::MyProc() == ParallelDescriptor::IOProcessorNumber()) {
        std::string filename = "CdCl_" + std::to_string(id) + ".dat";
        std::ofstream file(filename, std::ios::app);

        if (!file.is_open()) {
            std::cerr << "Cannot open the file: " << filename << std::endl;
            std::exit(1); // 错误退出
        }

        file << step << "\t" << fx << "\t" << fy << "\t" << fz << "\n";
        file.close();
    }
}
#endif

void LagrangeParticleContainer::WriteParticle(int step) {
    std::string filename = "particle_" + std::to_string(id);
    filename += '_';
    const std::string& pltfile = amrex::Concatenate(filename, step, 5);
    WriteAsciiFile(pltfile); // 这个没啥用，只能用来调试
}

void LagrangeParticleContainer::Checkpoint(const std::string& directory,
                                           const std::string& name) const {
    amrex::ParticleContainer<0, 0, PIdx::nattribs>::Checkpoint(directory, name);
    if (ParallelDescriptor::IOProcessor()) {
        std::ofstream out(directory + "/" + name + "/Geometry");
        out << std::setprecision(17) << "ParticleGeometryV1 " << AMREX_SPACEDIM
            << " " << AMREX_D_PICK("", "disk", "sphere") << " " << radius
            << " " << grid_.dx << " " << grid_.dx_min;
        for (int d = 0; d < AMREX_SPACEDIM; ++d) {
            out << " " << centre[d];
        }
        out << "\n";
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(out.good(), "cannot write particle geometry metadata");
    }
}

void LagrangeParticleContainer::Restart(const std::string& directory,
                                        const std::string& name) {
    std::ifstream in(directory + "/" + name + "/Geometry");
    if (in) {
        std::string version, shape;
        int dimension = 0;
        Real saved_radius = 0, saved_dx = 0, saved_dx_min = 0;
        RealVect saved_centre;
        in >> version >> dimension >> shape >> saved_radius >> saved_dx >> saved_dx_min;
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(in && version == "ParticleGeometryV1" &&
                                             dimension == AMREX_SPACEDIM && shape == AMREX_D_PICK("", "disk", "sphere"),
                                         "particle checkpoint dimension or shape mismatch");
        for (int d = 0; d < AMREX_SPACEDIM; ++d) {
            in >> saved_centre[d];
        }
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(in && std::isfinite(saved_radius) && saved_radius > 0,
                                         "invalid particle geometry metadata");
        for (int d = 0; d < AMREX_SPACEDIM; ++d) {
            AMREX_ALWAYS_ASSERT_WITH_MESSAGE(std::isfinite(saved_centre[d]), "invalid particle centre");
        }
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(saved_dx == grid_.dx && saved_dx_min == grid_.dx_min,
                                         "particle checkpoint grid spacing mismatch");
        radius = saved_radius;
        centre = saved_centre;
    } else {
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(AMREX_SPACEDIM == 3,
                                         "DIM=2 requires disk geometry metadata; legacy sphere checkpoints are forbidden");
    }
    amrex::ParticleContainer<0, 0, PIdx::nattribs>::Restart(directory, name);
}
