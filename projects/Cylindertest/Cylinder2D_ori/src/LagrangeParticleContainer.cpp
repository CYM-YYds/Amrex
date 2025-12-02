#include "LagrangeParticleContainer.H"
#include "D2Q9.H"
#include "Kernels.H"
#include <algorithm>  // for std::max_element, std::min_element
using namespace amrex;

void LagrangeParticleContainer::PrintParticleParm() {
    amrex::Print() << "╔══════════════════════════════════════════════════════╗" << std::endl;
    amrex::Print() << "║                      IBM PARAMETERS                  ║" << std::endl;
    amrex::Print() << "╚══════════════════════════════════════════════════════╝" << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  R      =" << std::setw(10) << std::right << R << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  Rm     =" << std::setw(10) << std::right << Rm << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  ds0    =" << std::setw(10) << std::right << ds0 << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  np     =" << std::setw(10) << std::right << np << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  area   =" << std::setw(10) << std::right << IB_weight << std::endl;

    amrex::Print() << "╚══════════════════════════════════════════════════════╝" << std::endl;
    amrex::Print() << std::endl;
}

void LagrangeParticleContainer::InitParticle(int lev) {
    ParticleType p;
    std::array<ParticleReal, PIdx::nattribs> attribs;
    const Real* delta = Geom(0).CellSize();

    int num_proc = ParallelDescriptor::NProcs();

    if (ParallelDescriptor::MyProc() == ParallelDescriptor::IOProcessorNumber()) {
        for (int i = 0; i < np; i++) {
            p.id() = ParticleType::NextID();
            p.cpu() = ParallelDescriptor::MyProc(); // int(p.id()%num_proc);
            p.pos(0) = (X + Rm * cos(2.0 * PI * (i) / np)) * delta[0];
            p.pos(1) = (Y + Rm * sin(2.0 * PI * (i) / np)) * delta[0];

            attribs[PIdx::fx] = 0.0;
            attribs[PIdx::fy] = 0.0;
            attribs[PIdx::ufx] = 0.0;
            attribs[PIdx::ufy] = 0.0;

            std::pair<int, int> key{0, 0};
            auto& particle_tile = GetParticles(0)[key];

            particle_tile.push_back(p);
            particle_tile.push_back_real(attribs);
        }
    }
    Redistribute();

    // ========== 打印粒子在不同 Box 中的分布 ==========
    amrex::Print() << "\n╔══════════════════════════════════════════════════════╗" << std::endl;
    amrex::Print() << "║         PARTICLE DISTRIBUTION ACROSS BOXES           ║" << std::endl;
    amrex::Print() << "╚══════════════════════════════════════════════════════╝" << std::endl;

    // 获取 BoxArray 信息
    const auto& ba = ParticleBoxArray(lev);
    const auto& dm = ParticleDistributionMap(lev);
    int nboxes = ba.size();
    int myproc = ParallelDescriptor::MyProc();
    int nprocs = ParallelDescriptor::NProcs();

    amrex::Print() << "Total number of Boxes: " << nboxes << std::endl;
    amrex::Print() << "Total number of MPI ranks: " << nprocs << std::endl;
    amrex::Print() << std::endl;

    // 统计每个 Box 中的粒子数
    std::vector<long> particles_per_box(nboxes, 0);
    long total_local = 0;

    for (MyParIter pti(*this, lev); pti.isValid(); ++pti) {
        int box_idx = pti.index();  // 当前 Box 的索引
        long np_in_box = pti.numParticles();
        particles_per_box[box_idx] = np_in_box;
        total_local += np_in_box;
    }

    // 收集所有进程的数据到 IOProc
    std::vector<long> global_particles_per_box(nboxes, 0);
    ParallelDescriptor::ReduceLongSum(particles_per_box.data(), nboxes);
    for (int i = 0; i < nboxes; ++i) {
        global_particles_per_box[i] = particles_per_box[i];
    }

    // 只在 IOProc 打印
    if (ParallelDescriptor::IOProcessor()) {
        long total_particles = 0;
        amrex::Print() << "┌──────────┬──────────┬──────────────────┬──────────────┐" << std::endl;
        amrex::Print() << "│  Box ID  │  CPU ID  │  Num Particles   │   Box Range  │" << std::endl;
        amrex::Print() << "├──────────┼──────────┼──────────────────┼──────────────┤" << std::endl;

        for (int i = 0; i < nboxes; ++i) {
            const Box& bx = ba[i];
            int cpu_id = dm[i];
            amrex::Print() << "│ " << std::setw(8) << i 
                          << " │ " << std::setw(8) << cpu_id
                          << " │ " << std::setw(16) << global_particles_per_box[i]
                          << " │ " << bx.smallEnd() << "-" << bx.bigEnd() << " │" << std::endl;
            total_particles += global_particles_per_box[i];
        }

        amrex::Print() << "├──────────┴──────────┼──────────────────┼──────────────┤" << std::endl;
        amrex::Print() << "│       TOTAL         │ " << std::setw(16) << total_particles << " │              │" << std::endl;
        amrex::Print() << "└─────────────────────┴──────────────────┴──────────────┘" << std::endl;

        // 计算分布统计
        long max_particles = *std::max_element(global_particles_per_box.begin(), global_particles_per_box.end());
        long min_particles = *std::min_element(global_particles_per_box.begin(), global_particles_per_box.end());
        int boxes_with_particles = 0;
        for (int i = 0; i < nboxes; ++i) {
            if (global_particles_per_box[i] > 0) boxes_with_particles++;
        }

        amrex::Print() << "\n[Statistics]" << std::endl;
        amrex::Print() << "  Boxes with particles: " << boxes_with_particles << " / " << nboxes << std::endl;
        amrex::Print() << "  Max particles in one box: " << max_particles << std::endl;
        amrex::Print() << "  Min particles in one box: " << min_particles << std::endl;
        if (boxes_with_particles > 0) {
            amrex::Print() << "  Average (non-empty boxes): " << total_particles / boxes_with_particles << std::endl;
        }
        amrex::Print() << "╚══════════════════════════════════════════════════════╝\n" << std::endl;
    }
    // ========== 打印结束 ==========
}

void LagrangeParticleContainer::MoveParticle() {
}

void LagrangeParticleContainer::InterpForce(int lev, amrex::MultiFab& rho_lev, amrex::MultiFab& u_lev, amrex::MultiFab& force_lev) {
    const Real delta = Geom(lev).CellSize()[0];

    for (MyParIter pti(*this, lev); pti.isValid(); ++pti) {
        auto& particles = pti.GetArrayOfStructs();
        auto p_ptr = particles().data();
        const long n = pti.numParticles();

        auto& attribs = pti.GetAttribs();
        auto fx = attribs[PIdx::fx].data();
        auto fy = attribs[PIdx::fy].data();
        auto ufx = attribs[PIdx::ufx].data();
        auto ufy = attribs[PIdx::ufy].data();

        const Array4<Real>& u = u_lev.array(pti);
        const Array4<Real>& rho = rho_lev.array(pti);
        const Array4<Real>& Ft = force_lev.array(pti);

        amrex::ParallelFor(n, [=] AMREX_GPU_DEVICE(int i) noexcept {
            force_interp_extrap(p_ptr[i], fx[i], fy[i], ufx[i], ufy[i], u, rho, Ft, delta);
        });
    }
}

void LagrangeParticleContainer::SaveFxy(int lev, int step) {
    using SPType = typename LagrangeParticleContainer::SuperParticleType;

    auto fx = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
        return p.rdata(PIdx::fx);
    });

    auto fy = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
        return p.rdata(PIdx::fy);
    });

    ParallelDescriptor::ReduceRealSum(fx);
    ParallelDescriptor::ReduceRealSum(fy);

    if (ParallelDescriptor::MyProc() == ParallelDescriptor::IOProcessorNumber()) {
        Real m = 0.5 * (p0 / cs2) * U0 * U0 * D * dx_0;
        fx /= m;
        fy /= m;
        std::string filename = "CdCl.dat";
        std::ofstream file(filename, step == 1 ? std::ios::trunc : std::ios::app);

        if (!file.is_open()) {
            std::cerr << "Cannot open the file: " << filename << std::endl;
            std::exit(1); // 错误退出
        }

        file << step << "\t" << fx << "\t" << fy << "\n";
        file.close();
    }
}

void LagrangeParticleContainer::WriteParticle(int step) {
    const std::string& pltfile = amrex::Concatenate("particles", step, 5);
    WriteAsciiFile(pltfile);
}
