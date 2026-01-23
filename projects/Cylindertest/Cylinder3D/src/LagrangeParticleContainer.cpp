#include "LagrangeParticleContainer.H"
#include "D3Q19.H"
#include "Kernels.H"
#include <algorithm> // for std::max_element, std::min_element
using namespace amrex;

void LagrangeParticleContainer::PrintParticleParm() {
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
    amrex::Print() << std::setw(15) << std::left << "  mp     =" << std::setw(10) << std::right << Mp << std::endl;
    // amrex::Print() << std::setw(15) << std::left << "  mf     =" << std::setw(10) << std::right << Mf << std::endl;
    amrex::Print() << std::setw(15) << std::left << "  Mp     =" << std::setw(10) << std::right << Mp_iner << std::endl;
    // amrex::Print() << std::setw(15) << std::left << "  Mf     =" << std::setw(10) << std::right << Mf_iner << std::endl;

    amrex::Print() << "╚══════════════════════════════════════════════════════╝" << std::endl;
    amrex::Print() << std::endl;
}

void LagrangeParticleContainer::InitParticle(int lev) {
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
                p.pos(2) = (centre[2]) * delta[0] + radius * dx_min;

                attribs[PIdx::xlocal] = 0.0;
                attribs[PIdx::ylocal] = 0.0;
                attribs[PIdx::zlocal] = radius * dx_min;

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

                    p.pos(0) = (centre[0]) * delta[0] + radius * sin(sita) * cos(alpha) * dx_min;
                    p.pos(1) = (centre[1]) * delta[0] + radius * sin(sita) * sin(alpha) * dx_min;
                    p.pos(2) = (centre[2]) * delta[0] + radius * cos(sita) * dx_min;

                    attribs[PIdx::xlocal] = radius * sin(sita) * cos(alpha) * dx_min;
                    attribs[PIdx::ylocal] = radius * sin(sita) * sin(alpha) * dx_min;
                    attribs[PIdx::zlocal] = radius * cos(sita) * dx_min;
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
                p.pos(2) = (centre[2]) * delta[0] - radius * dx_min;

                attribs[PIdx::xlocal] = 0.0;
                attribs[PIdx::ylocal] = 0.0;
                attribs[PIdx::zlocal] = -radius * dx_min;
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
}

void LagrangeParticleContainer::InitCylinderParticle(int lev) {
    // 生成沿 Y 轴的圆柱侧壁拉格朗日点（由原 Z 轴版本改写）
    // 圆柱轴心: (centre[0], centre[2])，半径: radius (索引单位)，高度方向为 Y ，使用 ns 层线性划分
    ParticleType p;
    std::array<ParticleReal, PIdx::nattribs> attribs;
    const Real* delta0 = Geom(0).CellSize();

    // 半径的物理尺度
    Real phys_radius = radius * dx_min;
    // 高度（索引→物理混用的原逻辑保持不变，仅改轴向变量名）
    Real height_index = 2.0 * NH;
    Real y_lo_index = centre[1] - height_index / 2.0;
    Real y_hi_index = centre[1] + height_index / 2.0;

    int ny_layers = nt; // 轴向层数（沿 Y）
    int nt_ring = nt1;  // 每层圆周点数
    if (nt_ring < 3)
        nt_ring = 3;

    // 侧壁总面积 ≈ 2*pi*R*H ，保持原 height_index 处理方式
    Real side_area = 2.0 * PI * phys_radius * (height_index * dx_min);
    Real area_per_side_point = side_area / (ny_layers * nt_ring);

    if (ParallelDescriptor::MyProc() == ParallelDescriptor::IOProcessorNumber()) {
        for (int iy = 0; iy < ny_layers; ++iy) {
            Real frac = (ny_layers == 1) ? 0.5 : (static_cast<Real>(iy) / (ny_layers - 1));
            Real y_index = y_lo_index + frac * (y_hi_index - y_lo_index); // 索引坐标
            Real y_phys = y_index * dx_min;                               // 物理坐标（与原逻辑一致）

            for (int j = 0; j < nt_ring; ++j) {
                Real alpha = 2.0 * PI * (static_cast<Real>(j) / nt_ring);
                Real x_offset = phys_radius * std::cos(alpha); // 环向偏移 X
                Real z_offset = phys_radius * std::sin(alpha); // 环向偏移 Z

                p.id() = ParticleType::NextID();
                p.cpu() = ParallelDescriptor::MyProc();
                p.pos(0) = centre[0] * dx_0 + x_offset; // 绝对物理坐标 X
                p.pos(1) = y_phys;                      // 绝对物理坐标 Y
                p.pos(2) = centre[2] * dx_0 + z_offset; // 绝对物理坐标 Z

                // 本地坐标：用于后续基于 centre 更新
                attribs[PIdx::xlocal] = x_offset;
                attribs[PIdx::ylocal] = y_phys - centre[1] * dx_0;
                attribs[PIdx::zlocal] = z_offset;

                attribs[PIdx::area] = area_per_side_point;
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

        amrex::Print() << "[InitCylinderParticle] (Axis=Y) Generated side-wall points: "
                       << (ny_layers * nt_ring) << " (R_physical=" << phys_radius
                       << ", H_physical=" << (height_index * dx_min) << ")" << std::endl;
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
        int box_idx = pti.index(); // 当前 Box 的索引
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

        // 按 CPU 统计
        std::vector<long> particles_per_cpu(nprocs, 0);
        std::vector<int> boxes_per_cpu(nprocs, 0);

        amrex::Print() << "┌──────────┬──────────┬──────────────────┬─────────────────────────────────┐" << std::endl;
        amrex::Print() << "│  Box ID  │  CPU ID  │  Num Particles   │           Box Range             │" << std::endl;
        amrex::Print() << "├──────────┼──────────┼──────────────────┼─────────────────────────────────┤" << std::endl;

        for (int i = 0; i < nboxes; ++i) {
            const Box& bx = ba[i];
            int cpu_id = dm[i];

            // 只打印有粒子的 Box (3D 可能 Box 很多)
            if (global_particles_per_box[i] > 0) {
                amrex::Print() << "│ " << std::setw(8) << i
                               << " │ " << std::setw(8) << cpu_id
                               << " │ " << std::setw(16) << global_particles_per_box[i]
                               << " │ " << bx.smallEnd() << "-" << bx.bigEnd() << " │" << std::endl;
            }
            total_particles += global_particles_per_box[i];
            particles_per_cpu[cpu_id] += global_particles_per_box[i];
            boxes_per_cpu[cpu_id]++;
        }

        amrex::Print() << "├──────────┴──────────┼──────────────────┼─────────────────────────────────┤" << std::endl;
        amrex::Print() << "│       TOTAL         │ " << std::setw(16) << total_particles << " │                                 │" << std::endl;
        amrex::Print() << "└─────────────────────┴──────────────────┴─────────────────────────────────┘" << std::endl;

        // 计算分布统计
        long max_particles = *std::max_element(global_particles_per_box.begin(), global_particles_per_box.end());
        long min_particles_nonzero = total_particles; // 非零最小值
        int boxes_with_particles = 0;
        for (int i = 0; i < nboxes; ++i) {
            if (global_particles_per_box[i] > 0) {
                boxes_with_particles++;
                if (global_particles_per_box[i] < min_particles_nonzero) {
                    min_particles_nonzero = global_particles_per_box[i];
                }
            }
        }

        amrex::Print() << "\n[Box Statistics]" << std::endl;
        amrex::Print() << "  Boxes with particles: " << boxes_with_particles << " / " << nboxes << std::endl;
        amrex::Print() << "  Max particles in one box: " << max_particles << std::endl;
        amrex::Print() << "  Min particles (non-empty): " << min_particles_nonzero << std::endl;
        if (boxes_with_particles > 0) {
            amrex::Print() << "  Average (non-empty boxes): " << total_particles / boxes_with_particles << std::endl;
        }

        // 按 CPU 统计
        amrex::Print() << "\n[CPU Load Distribution]" << std::endl;
        amrex::Print() << "┌──────────┬──────────┬──────────────────┐" << std::endl;
        amrex::Print() << "│  CPU ID  │  Boxes   │  Total Particles │" << std::endl;
        amrex::Print() << "├──────────┼──────────┼──────────────────┤" << std::endl;
        for (int i = 0; i < nprocs; ++i) {
            amrex::Print() << "│ " << std::setw(8) << i
                           << " │ " << std::setw(8) << boxes_per_cpu[i]
                           << " │ " << std::setw(16) << particles_per_cpu[i] << " │" << std::endl;
        }
        amrex::Print() << "└──────────┴──────────┴──────────────────┘" << std::endl;

        long max_cpu_particles = *std::max_element(particles_per_cpu.begin(), particles_per_cpu.end());
        long min_cpu_particles = *std::min_element(particles_per_cpu.begin(), particles_per_cpu.end());
        double imbalance = (max_cpu_particles > 0) ? (static_cast<double>(max_cpu_particles - min_cpu_particles) / max_cpu_particles * 100.0) : 0.0;

        amrex::Print() << "  Load imbalance: " << std::fixed << std::setprecision(1) << imbalance << "%" << std::endl;
        amrex::Print() << "╚══════════════════════════════════════════════════════╝\n"
                       << std::endl;
    }
    // ========== 打印结束 ==========
}

void LagrangeParticleContainer::InitChannelParticle(int lev) {
    // 在通道上下壁面均匀铺设拉格朗日点，用于 IBM 壁面处理
    // 目标间距：0.5 * dx_min（物理长度），在 x-z 平面均匀分布

    ParticleType p;
    std::array<ParticleReal, PIdx::nattribs> attribs;

    // 索引空间的起止（以单元中心为 0.5, NX-0.5 等）
    // const Real x_lo_idx = 3;
    // const Real x_hi_idx = NX - 4;
    // const Real z_lo_idx = 3;
    // const Real z_hi_idx = NZ - 4;

    const Real x_lo_idx = 0;
    const Real x_hi_idx = NX - 0.5;
    const Real z_lo_idx = 0;
    const Real z_hi_idx = NZ - 0.5;

    // 上下壁面的 j 索引位置（单元中心）
    const Real y_bot_idx = 3.5;
    const Real y_top_idx = NY - 3.5;

    // 采用索引步长（更稳妥）：索引增量 0.5，对应物理间距 0.5*dx_min
    const Real s_idx_x = 0.5;
    const Real s_idx_z = 0.5;

    // 每个点对应的“索引面积”（以格点面为单位），后续力学量会乘到物理尺度
    const Real area_per_point = s_idx_x * s_idx_z;

    // 仅在 I/O 进程上创建，再用 Redistribute 分发
    if (ParallelDescriptor::MyProc() == ParallelDescriptor::IOProcessorNumber()) {
        // 避免浮点误差导致漏掉末端
        const Real eps = 1e-12;

        for (Real xi = x_lo_idx; xi <= x_hi_idx + eps; xi += s_idx_x) {
            // 修正最后一个点，避免越界
            Real xi_clamped = amrex::min(xi, x_hi_idx);
            Real x_phys = xi_clamped * dx_min;

            for (Real zi = z_lo_idx; zi <= z_hi_idx + eps; zi += s_idx_z) {
                Real zi_clamped = amrex::min(zi, z_hi_idx);
                Real z_phys = zi_clamped * dx_min;

                // 下壁面点
                {
                    p.id() = ParticleType::NextID();
                    p.cpu() = ParallelDescriptor::MyProc();
                    p.pos(0) = x_phys;
                    p.pos(1) = y_bot_idx * dx_min;
                    p.pos(2) = z_phys;

                    // xlocal/ylocal/zlocal = 绝对位置 - centre*dx_0，使 MoveParticle 时位置保持一致
                    attribs[PIdx::xlocal] = p.pos(0) - centre[0] * dx_0;
                    attribs[PIdx::ylocal] = p.pos(1) - centre[1] * dx_0;
                    attribs[PIdx::zlocal] = p.pos(2) - centre[2] * dx_0;

                    attribs[PIdx::area] = area_per_point;
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

                // 上壁面点
                {
                    p.id() = ParticleType::NextID();
                    p.cpu() = ParallelDescriptor::MyProc();
                    p.pos(0) = x_phys;
                    p.pos(1) = y_top_idx * dx_min;
                    p.pos(2) = z_phys;

                    attribs[PIdx::xlocal] = p.pos(0) - centre[0] * dx_0;
                    attribs[PIdx::ylocal] = p.pos(1) - centre[1] * dx_0;
                    attribs[PIdx::zlocal] = p.pos(2) - centre[2] * dx_0;

                    attribs[PIdx::area] = area_per_point;
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
    }

    // 分发到对应盒与进程
    Redistribute();
}

void LagrangeParticleContainer::InitParticleFromFile(int lev, const std::string object) {
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

            for (int j = 0; j < num_points; ++j) {
                amrex::Real pos_x, pos_y, pos_z, area_tmp;
                amrex::Real dir_x, dir_y, dir_z;

                fscanf(fp, "%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\n",
                       &pos_x, &pos_y, &pos_z, &area_tmp, &dir_x, &dir_x, &dir_x);

                p.id() = ParticleType::NextID();
                p.cpu() = ParallelDescriptor::MyProc();
                p.pos(0) = (centre[0]) * delta[0] + pos_x / factor * dx_min;
                p.pos(1) = (centre[1]) * delta[0] + pos_y / factor * dx_min;
                p.pos(2) = (centre[2]) * delta[0] + pos_z / factor * dx_min;

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
}

void LagrangeParticleContainer::MoveParticle(int lev, amrex::Real cur_time) {
    amrex::RealVect pos_dif, pos_new;
    amrex::RealVect vel_new, angvel_new;
    amrex::RealVect G_dire{0.0, 0.0, -1.0};
    amrex::Real Fgra = (1.0 - rhof / rhop) * Mp * G;

    for (int i = 0; i < AMREX_SPACEDIM; i++) {
        // F_lub[i] = 0.0;

        vel_new[i] = (1.0 + rhof / rhop) * vel[i] - (rhof / rhop) * vel_old[i] + (F_tot[i] + Fgra * G_dire[i] + F_lub[i]) / Mp * dt_min;
        pos_new[i] = centre[i] + dt_min * 0.5 * (vel_new[i] + vel[i]) / dx_0;
        vel_old[i] = vel[i];
        vel[i] = vel_new[i];

        angvel_new[i] = (1.0 + rhof / rhop) * angvel[i] - (rhof / rhop) * angvel_old[i] + T_tot[i] * dt_min / Mp_iner;
        angvel_old[i] = angvel[i];
        angvel[i] = angvel_new[i];
    }

    // 更新拉格朗日点绝对位置
    pos_dif[0] = (pos_new[0] - centre[0]) * dx_0;
    pos_dif[1] = (pos_new[1] - centre[1]) * dx_0;
    pos_dif[2] = (pos_new[2] - centre[2]) * dx_0;

    // amrex::Print() << "dif_x = " << pos_dif[0] << ", " << "dif_y = " << pos_dif[1] << ", " << "dif_z = " << pos_dif[2] << std::endl;

    centre[0] = pos_new[0];
    centre[1] = pos_new[1];
    centre[2] = pos_new[2];

    const Real delta = Geom(lev).CellSize()[0];

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
            p_ptr[i].pos(0) = centre_pos[0] * dx_0 + xlocal[i];
            p_ptr[i].pos(1) = centre_pos[1] * dx_0 + ylocal[i];
            p_ptr[i].pos(2) = centre_pos[2] * dx_0 + zlocal[i]; });
    }
}

amrex::RealVect LagrangeParticleContainer::ReturnCentre() {
    return centre;
}

amrex::RealVect LagrangeParticleContainer::ReturnVelocity() {
    return vel;
}

//============================================================================//
//                     MDF (Multi-Direct Forcing) 实现                        //
//============================================================================//
// 清零粒子力（用于迭代开始前）
void LagrangeParticleContainer::ZeroParticleForce(int lev) {
    for (MyParIter pti(*this, lev); pti.isValid(); ++pti) {
        const long n = pti.numParticles();

        auto& attribs = pti.GetAttribs();
        auto fx = attribs[PIdx::fx].data();
        auto fy = attribs[PIdx::fy].data();
        auto fz = attribs[PIdx::fz].data();

        amrex::ParallelFor(n, [=] AMREX_GPU_DEVICE(int i) noexcept {
            fx[i] = 0.0;
            fy[i] = 0.0;
            fz[i] = 0.0;
        });
    }
}

#if USE_MDF_TWO_STAGE
// MDF 两阶段迭代版本（NF > 1）：力增量写入 force_delta_lev，累积力读取自 force_lev
void LagrangeParticleContainer::InterpForce(int lev, amrex::MultiFab& rho_lev, amrex::MultiFab& u_lev,
                                            amrex::MultiFab& force_lev, amrex::MultiFab& force_delta_lev) {
    const Real delta = Geom(lev).CellSize()[0];

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
        const Array4<Real>& Ft_delta = force_delta_lev.array(pti);

        const RealVect& uc = vel;
        const RealVect& wc = angvel;
        const RealVect& pos = centre;

        amrex::ParallelFor(n, [=] AMREX_GPU_DEVICE(int i) noexcept {
            force_interp_extrap(p_ptr[i], fx[i], fy[i], fz[i], tx[i], ty[i], tz[i],
                                xlocal[i], ylocal[i], zlocal[i], area[i],
                                u, rho, Ft, Ft_delta, delta, uc, wc, pos);
        });
    }

    // 统计转矩等颗粒受力（每次迭代后累加）
    using SPType = typename LagrangeParticleContainer::SuperParticleType;

    auto fx = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal { return p.rdata(PIdx::fx); });
    auto fy = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal { return p.rdata(PIdx::fy); });
    auto fz = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal { return p.rdata(PIdx::fz); });
    auto tx = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal { return p.rdata(PIdx::tx); });
    auto ty = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal { return p.rdata(PIdx::ty); });
    auto tz = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal { return p.rdata(PIdx::tz); });

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
#else
// MDF 单次迭代版本（NF = 1）：直接计算力并写入 force_lev
void LagrangeParticleContainer::InterpForce(int lev, amrex::MultiFab& rho_lev, amrex::MultiFab& u_lev, amrex::MultiFab& force_lev) {
    const Real delta = Geom(lev).CellSize()[0];

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

        amrex::ParallelFor(n, [=] AMREX_GPU_DEVICE(int i) noexcept { force_interp_extrap(p_ptr[i], fx[i], fy[i], fz[i], tx[i], ty[i], tz[i], xlocal[i], ylocal[i], zlocal[i], area[i], u, rho, Ft, delta, uc, wc, pos); });
    }

    // 统计转矩等颗粒受力
    using SPType = typename LagrangeParticleContainer::SuperParticleType;

    auto fx = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal { return p.rdata(PIdx::fx); });

    auto fy = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal { return p.rdata(PIdx::fy); });

    auto fz = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal { return p.rdata(PIdx::fz); });

    auto tx = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal { return p.rdata(PIdx::tx); });

    auto ty = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal { return p.rdata(PIdx::ty); });
    auto tz = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal { return p.rdata(PIdx::tz); });

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
void LagrangeParticleContainer::InterpForceWallModel(int lev, amrex::MultiFab& rho_lev, amrex::MultiFab& u_lev, amrex::MultiFab& force_lev) {
    const Real delta = Geom(lev).CellSize()[0];

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

        amrex::ParallelFor(n, [=] AMREX_GPU_DEVICE(int i) noexcept { force_wall_model(p_ptr[i], fx[i], fy[i], fz[i], tx[i], ty[i], tz[i], xlocal[i], ylocal[i], zlocal[i], area[i],
                                                                                      u, rho, Ft, delta, uc, wc, pos); });
    }

    // 统计转矩等颗粒受力
    using SPType = typename LagrangeParticleContainer::SuperParticleType;

    auto fx = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal { return p.rdata(PIdx::fx); });

    auto fy = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal { return p.rdata(PIdx::fy); });

    auto fz = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal { return p.rdata(PIdx::fz); });

    auto tx = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal { return p.rdata(PIdx::tx); });

    auto ty = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal { return p.rdata(PIdx::ty); });
    auto tz = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal { return p.rdata(PIdx::tz); });

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
    amrex::Real Fgra = (1.0 - 1.0 * rhof / rhop) * Mp * G * dx_min; // 调整了大小不要忘记
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

void LagrangeParticleContainer::CollideWall() {
    amrex::RealVect Fpw_lub = {0.0, 0.0, 0.0};

    amrex::RealVect pos_p1 = this->ReturnCentre();
    amrex::RealVect vel_p1 = this->ReturnVelocity();

    amrex::Real lxc, lyc, lzc, lc;
    amrex::Real Ftmp, npw1, npw2, xc_fict;

    amrex::Real Fgra = (1.0 - rhof / rhop) * Mp * G * dx_min;

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
    if (pos_p1[0] >= (NX - R - safe - 1.0)) {
        xc_fict = NX + R - 1.0;
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
    if (pos_p1[1] >= (NY - R - safe - 1.0)) {
        xc_fict = NY + R - 1.0; // 这里到底是多少
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
    if (pos_p1[2] >= (NZ - R - safe - 1.0)) {
        xc_fict = NZ + R - 1.0; // 这里到底是多少
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

void LagrangeParticleContainer::SetLubVal(amrex::RealVect& force_lub) {
    F_lub[0] += force_lub[0];
    F_lub[1] += force_lub[1];
    F_lub[2] += force_lub[2];
}

void LagrangeParticleContainer::SaveVelocity(int lev, int step) {
    amrex::Real uz = vel[2] * U0;

    if (ParallelDescriptor::MyProc() == ParallelDescriptor::IOProcessorNumber()) {
        std::string filename = "data/vel_" + std::to_string(id) + ".dat";
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
    amrex::Real pos_z = centre[2];

    if (ParallelDescriptor::MyProc() == ParallelDescriptor::IOProcessorNumber()) {
        std::string filename = "data/pos_" + std::to_string(id) + ".dat";
        std::ofstream file(filename, std::ios::app);

        if (!file.is_open()) {
            std::cerr << "Cannot open the file: " << filename << std::endl;
            std::exit(1); // 错误退出
        }

        file << step << "\t" << pos_x << "\t" << pos_y << "\t" << pos_z << "\n";
        file.close();
    }
}

void LagrangeParticleContainer::SaveFxy(int lev, int step) {
    using SPType = typename LagrangeParticleContainer::SuperParticleType;

    auto fx = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal { return p.rdata(PIdx::fx); });

    auto fy = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal { return p.rdata(PIdx::fy); });

    auto fz = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal { return p.rdata(PIdx::fz); });

    ParallelDescriptor::ReduceRealSum(fx);
    ParallelDescriptor::ReduceRealSum(fy);
    ParallelDescriptor::ReduceRealSum(fz);

    amrex::Real m = 0.5 * (rho0)*U0 * U0 * PI * D * dx_0 * D * dx_0 / 4.0;
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
        std::string filename = "data/CdCl_" + std::to_string(id) + ".dat";
        std::ofstream file(filename, std::ios::app);

        if (!file.is_open()) {
            std::cerr << "Cannot open the file: " << filename << std::endl;
            std::exit(1); // 错误退出
        }

        file << step << "\t" << fx << "\t" << fy << "\t" << fz << "\n";
        file.close();
    }
}

void LagrangeParticleContainer::WriteParticle(int step) {
    std::string filename = "particle_" + std::to_string(id);
    filename += '_';
    const std::string& pltfile = amrex::Concatenate(filename, step, 5);
    WriteAsciiFile(pltfile); // 这个没啥用，只能用来调试
}
//********************************************************************//
//                    IDF (Implicit Direct Forcing) 实现             //
//********************************************************************//

int LagrangeParticleContainer::IDF_CollectParticleData(int lev,
                                                       std::vector<Real>& local_lag_pos,
                                                       std::vector<int>& local_lag_ids) {
    local_lag_pos.clear();
    local_lag_ids.clear();
    int local_NL = 0;

    for (MyParIter pti(*this, lev); pti.isValid(); ++pti) {
        auto& aos = pti.GetArrayOfStructs();
        const long n = pti.numParticles();
        if (n == 0)
            continue;

        // 将粒子数据从 GPU 复制到 CPU
        using ParticleType = typename LagrangeParticleContainer::ParticleType;
        amrex::Gpu::PinnedVector<ParticleType> host_particles(n);
        amrex::Gpu::copyAsync(amrex::Gpu::deviceToHost,
                              aos().begin(), aos().end(),
                              host_particles.begin());
        amrex::Gpu::streamSynchronize();

        for (long i = 0; i < n; ++i) {
            Real xt_phys = host_particles[i].pos(0);
            Real yt_phys = host_particles[i].pos(1);
            Real zt_phys = host_particles[i].pos(2);
            int pid = host_particles[i].id();
            local_lag_pos.push_back(xt_phys);
            local_lag_pos.push_back(yt_phys);
            local_lag_pos.push_back(zt_phys);
            local_lag_ids.push_back(pid);
            local_NL++;
        }
    }
    return local_NL;
}

void LagrangeParticleContainer::IDF_Interpolate(int lev,
                                                MultiFab& u_lev,
                                                MultiFab& rho_lev) {
    const Real delta = Geom(lev).CellSize()[0];

    for (MyParIter pti(*this, lev); pti.isValid(); ++pti) {
        auto& particles = pti.GetArrayOfStructs();
        auto p_ptr = particles().data();
        const long n = pti.numParticles();

        auto& attribs = pti.GetAttribs();
        auto ufx = attribs[PIdx::xlocal].data();
        auto ufy = attribs[PIdx::ylocal].data();
        auto ufz = attribs[PIdx::zlocal].data();
        auto rho_attr = attribs[PIdx::area].data();

        const Array4<Real>& u = u_lev.array(pti);
        const Array4<Real>& rho = rho_lev.array(pti);

        amrex::ParallelFor(n, [=] AMREX_GPU_DEVICE(int i) noexcept {
            ibm_interpolate(p_ptr[i], u, rho, delta, ufx[i], ufy[i], ufz[i], rho_attr[i]);
        });
    }
    amrex::Gpu::streamSynchronize();
}

void LagrangeParticleContainer::IDF_ReadInterpResults(int lev,
                                                      std::vector<Real>& local_interp_ux,
                                                      std::vector<Real>& local_interp_uy,
                                                      std::vector<Real>& local_interp_uz,
                                                      std::vector<Real>& local_interp_rho,
                                                      std::vector<int>* local_interp_ids) {
    using ParticleType = typename LagrangeParticleContainer::ParticleType;
    // 首先计算本进程粒子总数
    long total_n = 0;
    for (MyParIter pti(*this, lev); pti.isValid(); ++pti) {
        total_n += pti.numParticles();
    }

    local_interp_ux.resize(total_n);
    local_interp_uy.resize(total_n);
    local_interp_uz.resize(total_n);
    local_interp_rho.resize(total_n);
    if (local_interp_ids) {
        local_interp_ids->resize(total_n);
    }
    int local_idx = 0;

    for (MyParIter pti(*this, lev); pti.isValid(); ++pti) {
        const long n = pti.numParticles();
        if (n == 0)
            continue;

        auto& attribs = pti.GetAttribs();

        amrex::Gpu::PinnedVector<Real> host_ufx(n), host_ufy(n), host_ufz(n), host_rho(n);
        amrex::Gpu::copyAsync(amrex::Gpu::deviceToHost,
                              attribs[PIdx::xlocal].begin(), attribs[PIdx::xlocal].begin() + n,
                              host_ufx.begin());
        amrex::Gpu::copyAsync(amrex::Gpu::deviceToHost,
                              attribs[PIdx::ylocal].begin(), attribs[PIdx::ylocal].begin() + n,
                              host_ufy.begin());
        amrex::Gpu::copyAsync(amrex::Gpu::deviceToHost,
                              attribs[PIdx::zlocal].begin(), attribs[PIdx::zlocal].begin() + n,
                              host_ufz.begin());
        amrex::Gpu::copyAsync(amrex::Gpu::deviceToHost,
                              attribs[PIdx::area].begin(), attribs[PIdx::area].begin() + n,
                              host_rho.begin());

        // 如果需要粒子ID，同时拷贝AoS数据
        amrex::Gpu::PinnedVector<ParticleType> host_particles;
        if (local_interp_ids) {
            auto& aos = pti.GetArrayOfStructs();
            host_particles.resize(n);
            amrex::Gpu::copyAsync(amrex::Gpu::deviceToHost,
                                  aos().begin(), aos().end(),
                                  host_particles.begin());
        }
        amrex::Gpu::streamSynchronize();

        for (long i = 0; i < n; ++i) {
            local_interp_ux[local_idx + i] = host_ufx[i];
            local_interp_uy[local_idx + i] = host_ufy[i];
            local_interp_uz[local_idx + i] = host_ufz[i];
            local_interp_rho[local_idx + i] = host_rho[i];
            if (local_interp_ids) {
                (*local_interp_ids)[local_idx + i] = host_particles[i].id();
            }
        }
        local_idx += n;
    }
}

void LagrangeParticleContainer::IDF_WriteForceToParticles(int lev,
                                                          const std::vector<Real>& sol_x,
                                                          const std::vector<Real>& sol_y,
                                                          const std::vector<Real>& sol_z,
                                                          int idf_NL_global,
                                                          const std::unordered_map<int, int>& pid_to_idx) {
    const Real ib_coeff = dx_min * dx_min * dx_min; // 3D: 面积积分
    int NL = static_cast<int>(sol_x.size());

    // 将力密度数据复制到 GPU 可访问的内存
    amrex::Gpu::DeviceVector<Real> d_sol_x(NL);
    amrex::Gpu::DeviceVector<Real> d_sol_y(NL);
    amrex::Gpu::DeviceVector<Real> d_sol_z(NL);
    amrex::Gpu::copyAsync(amrex::Gpu::hostToDevice,
                          sol_x.data(), sol_x.data() + NL,
                          d_sol_x.data());
    amrex::Gpu::copyAsync(amrex::Gpu::hostToDevice,
                          sol_y.data(), sol_y.data() + NL,
                          d_sol_y.data());
    amrex::Gpu::copyAsync(amrex::Gpu::hostToDevice,
                          sol_z.data(), sol_z.data() + NL,
                          d_sol_z.data());
    amrex::Gpu::streamSynchronize();
    const Real* d_sol_x_ptr = d_sol_x.data();
    const Real* d_sol_y_ptr = d_sol_y.data();
    const Real* d_sol_z_ptr = d_sol_z.data();

    for (MyParIter pti(*this, lev); pti.isValid(); ++pti) {
        auto& aos = pti.GetArrayOfStructs();
        const long n = pti.numParticles();
        if (n == 0)
            continue;

        auto& attribs = pti.GetAttribs();
        auto fx_attr = attribs[PIdx::fx].data();
        auto fy_attr = attribs[PIdx::fy].data();
        auto fz_attr = attribs[PIdx::fz].data();

        // 使用pid_to_idx映射表计算全局索引（支持多球体）
        using ParticleType = typename LagrangeParticleContainer::ParticleType;
        amrex::Gpu::PinnedVector<ParticleType> host_particles(n);
        amrex::Gpu::copyAsync(amrex::Gpu::deviceToHost,
                              aos().begin(), aos().end(),
                              host_particles.begin());
        amrex::Gpu::streamSynchronize();

        // 构建本 tile 的全局索引数组（使用映射表）
        amrex::Gpu::PinnedVector<int> h_global_indices(n);
        for (long i = 0; i < n; ++i) {
            int pid = host_particles[i].id();
            auto it = pid_to_idx.find(pid);
            if (it != pid_to_idx.end()) {
                h_global_indices[i] = it->second - 1;  // 使用映射表，转换为0-based数组索引
            } else {
                amrex::Print() << "[IDF_3D][ERROR] particle ID " << pid
                               << " not found in pid_to_idx map!" << std::endl;
                h_global_indices[i] = 0;  // 默认值
            }
        }

        // 复制索引数组到 GPU
        amrex::Gpu::DeviceVector<int> d_global_indices(n);
        amrex::Gpu::copyAsync(amrex::Gpu::hostToDevice,
                              h_global_indices.begin(), h_global_indices.end(),
                              d_global_indices.begin());
        amrex::Gpu::streamSynchronize();
        const int* d_idx_ptr = d_global_indices.data();

        // 使用正确的全局索引写入粒子力属性
        amrex::ParallelFor(n, [=] AMREX_GPU_DEVICE(int i) noexcept {
            int global_idx = d_idx_ptr[i];
            fx_attr[i] = d_sol_x_ptr[global_idx] * ib_coeff;
            fy_attr[i] = d_sol_y_ptr[global_idx] * ib_coeff;
            fz_attr[i] = d_sol_z_ptr[global_idx] * ib_coeff;
        });
    }
    amrex::Gpu::streamSynchronize();
}

void LagrangeParticleContainer::IDF_SpreadForce(int lev, MultiFab& force_lev) {
    const Real delta = Geom(lev).CellSize()[0];

    for (MyParIter pti(*this, lev); pti.isValid(); ++pti) {
        auto& particles = pti.GetArrayOfStructs();
        auto p_ptr = particles().data();
        const long n = pti.numParticles();
        auto& attribs = pti.GetAttribs();
        auto fx = attribs[PIdx::fx].data();
        auto fy = attribs[PIdx::fy].data();
        auto fz = attribs[PIdx::fz].data();
        Array4<Real> const& Ft = force_lev.array(pti);

        amrex::ParallelFor(n, [=] AMREX_GPU_DEVICE(int i) noexcept {
            ibm_spread_force(p_ptr[i], fx[i], fy[i], fz[i], Ft, delta);
        });
    }
    amrex::Gpu::streamSynchronize();
}