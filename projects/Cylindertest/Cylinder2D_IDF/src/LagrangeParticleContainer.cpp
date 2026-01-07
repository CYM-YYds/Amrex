#include "LagrangeParticleContainer.H"
#include "D2Q9.H"
#include "Kernels.H"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <vector>
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
            attribs[PIdx::rho_interp] = p0 / cs2; // 初始化为参考密度

            std::pair<int, int> key{0, 0};
            auto& particle_tile = GetParticles(0)[key];

            particle_tile.push_back(p);
            particle_tile.push_back_real(attribs);
        }
    }
    Redistribute();
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

bool LagrangeParticleContainer::SaveFxy(int lev, int step, amrex::MultiFab& u_lev, amrex::MultiFab& rho_lev) {
    using SPType = typename LagrangeParticleContainer::SuperParticleType;
    using ParticleType = typename LagrangeParticleContainer::ParticleType;

    auto fx = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
        return p.rdata(PIdx::fx);
    });

    auto fy = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
        return p.rdata(PIdx::fy);
    });

    ParallelDescriptor::ReduceRealSum(fx);
    ParallelDescriptor::ReduceRealSum(fy);

    bool steady_reached = false;
    if (ParallelDescriptor::MyProc() == ParallelDescriptor::IOProcessorNumber()) {
        Real m = 0.5 * (p0 / cs2) * U0 * U0 * D * dx_0;
        fx /= m;
        fy /= m;

        const std::string filename = "CdCl.dat";
        std::ofstream file(filename, step == 1 ? std::ios::trunc : std::ios::app);

        if (!file.is_open()) {
            std::cerr << "Cannot open the file: " << filename << std::endl;
            std::exit(1); // 错误退出
        }

        file << step << "\t" << fx << "\t" << fy << "\n";
        file.close();

        // IO 进程仅进行Cd/Cl写出；稳态判定与Cp统计改由独立函数处理
    }
    // 将稳态判断与Cp输出委托给新函数（传入 MultiFab 以便在需要时执行插值更新）
    steady_reached = EvaluateConvergenceAndWriteCp(lev, step, fx, fy, u_lev, rho_lev);
    return steady_reached;
}

void LagrangeParticleContainer::WriteParticle(int step) {
    const std::string& pltfile = amrex::Concatenate("particles", step, 5);
    WriteAsciiFile(pltfile);
}

// 新增：稳态判断与压力系数统计
bool LagrangeParticleContainer::EvaluateConvergenceAndWriteCp(int lev, int step, Real cd_value, Real cl_value, amrex::MultiFab& u_lev, amrex::MultiFab& rho_lev) {
    using ParticleType = typename LagrangeParticleContainer::ParticleType;

    // 近期采样Cd历史与标志（跨多次调用保持）
    static std::vector<Real> cd_history(30);
    static bool steady_reached = false;
    static bool cp_written = false;

    // 每100步评估一次收敛：最近30个采样内相对波动<0.1%
    if (!steady_reached && step % 100 == 0) {
        static int current_idx = 0;
        static bool filled_once = false;

        cd_history[current_idx] = cd_value;
        current_idx = (current_idx + 1) % 30;
        if (current_idx == 0)
            filled_once = true;

        if (filled_once) {
            auto minmax_cd = std::minmax_element(cd_history.begin(), cd_history.end());
            const Real avg_cd = std::accumulate(cd_history.begin(), cd_history.end(), Real(0.0)) / 30.0;
            const Real span_cd = *minmax_cd.second - *minmax_cd.first;
            const Real rel_span = span_cd / std::max(std::abs(avg_cd), Real(1.0e-12));

            if (rel_span < 1.0e-3) {
                steady_reached = true;
                if (ParallelDescriptor::IOProcessor()) {
                    amrex::Print() << "Steady state reached at step " << step
                                   << ", Cd = " << cd_value
                                   << ", Cl = " << cl_value << std::endl;
                }
            }
        }
    }

    // 广播稳态标志，所有进程统一进入后续收集逻辑
    int steady_flag = steady_reached ? 1 : 0;
    ParallelDescriptor::Bcast(&steady_flag, 1, ParallelDescriptor::IOProcessorNumber());
    steady_reached = (steady_flag == 1);

    // 稳态后收集并输出圆柱表面压力系数分布
    if (steady_reached && !cp_written) {
        // 在收集 Cp 之前，先确保粒子上的 rho_interp 是最新的（仅在需要时执行插值以节省开销）
        IDF_Interpolate_normal_offset(lev, u_lev, rho_lev);
        // 此处需与粒子坐标保持一致，否则角度会被偏移
        const Real delta0 = Geom(0).CellSize()[0];
        const Real center_x = X * delta0;
        const Real center_y = Y * delta0;
        const Real p_ref = p0;
        const Real q_inf = 0.5 * (p0 / cs2) * U0 * U0;

        std::vector<Real> local_data;
        for (MyParIter pti(*this, lev); pti.isValid(); ++pti) {
            const long n = pti.numParticles();
            if (n == 0)
                continue;

            auto& aos = pti.GetArrayOfStructs();
            amrex::Gpu::PinnedVector<ParticleType> host_particles(n);
            amrex::Gpu::copyAsync(amrex::Gpu::deviceToHost,
                                  aos().begin(), aos().end(),
                                  host_particles.begin());

            auto& attribs = pti.GetAttribs();
            amrex::Gpu::PinnedVector<Real> host_rho(n);
            amrex::Gpu::copyAsync(amrex::Gpu::deviceToHost,
                                  attribs[PIdx::rho_interp].begin(),
                                  attribs[PIdx::rho_interp].begin() + n,
                                  host_rho.begin());
            amrex::Gpu::streamSynchronize();

            for (long i = 0; i < n; ++i) {
                const Real px = host_particles[i].pos(0);
                const Real py = host_particles[i].pos(1);
                const Real theta = std::atan2(py - center_y, px - center_x);
                const Real cp = (host_rho[i] - p_ref) / q_inf;
                local_data.push_back(theta);
                local_data.push_back(cp);
            }
        }

        const int nprocs = ParallelDescriptor::NProcs();
        const int iorank = ParallelDescriptor::IOProcessorNumber();
        const int local_count = static_cast<int>(local_data.size());

        std::vector<int> recv_counts(nprocs, 0);
        ParallelDescriptor::Gather(&local_count, 1, recv_counts.data(), 1, iorank);

        std::vector<int> displs(nprocs, 0);
        int total = 0;
        if (ParallelDescriptor::MyProc() == iorank) {
            for (int r = 0; r < nprocs; ++r) {
                displs[r] = total;
                total += recv_counts[r];
            }
        }

        std::vector<Real> gathered(total);
        ParallelDescriptor::Gatherv(local_data.data(), local_count,
                                    gathered.data(), recv_counts, displs, iorank);

        if (ParallelDescriptor::IOProcessor()) {
            std::vector<std::array<Real, 2>> entries;
            entries.reserve(total / 2);
            for (int k = 0; k < total / 2; ++k) {
                entries.push_back({gathered[2 * k], gathered[2 * k + 1]});
            }

            std::sort(entries.begin(), entries.end(),
                      [](const auto& a, const auto& b) { return a[0] < b[0]; });

            std::ofstream cp_file("Cp_steady.dat", std::ios::trunc);
            if (cp_file.is_open()) {
                cp_file << "# theta(rad)\tCp\n";
                for (const auto& e : entries) {
                    cp_file << e[0] << "\t" << e[1] << "\n";
                }
                cp_file.close();
                amrex::Print() << "[Cp] Steady-state Cp written ("
                               << entries.size() << " points)" << std::endl;
            }
        }

        cp_written = true;
        ParallelDescriptor::Barrier();
    }

    return steady_reached;
}

//********************************************************************//
//                     IDF 相关粒子操作实现                             //
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
            int pid = host_particles[i].id();
            local_lag_pos.push_back(xt_phys);
            local_lag_pos.push_back(yt_phys);
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
        auto ufx = attribs[PIdx::ufx].data();
        auto ufy = attribs[PIdx::ufy].data();
        auto rho_attr = attribs[PIdx::rho_interp].data();
        const Array4<Real>& u = u_lev.array(pti);
        const Array4<Real>& rho = rho_lev.array(pti);

        amrex::ParallelFor(n, [=] AMREX_GPU_DEVICE(int i) noexcept {
            ibm_interpolate(p_ptr[i], u, rho, delta, ufx[i], ufy[i], rho_attr[i]);
        });
    }
    amrex::Gpu::streamSynchronize();
}

void LagrangeParticleContainer::IDF_Interpolate_normal_offset(int lev,
                                                MultiFab& u_lev,
                                                MultiFab& rho_lev) {
    const Real delta = Geom(lev).CellSize()[0];

    for (MyParIter pti(*this, lev); pti.isValid(); ++pti) {
        auto& particles = pti.GetArrayOfStructs();
        auto p_ptr = particles().data();
        const long n = pti.numParticles();

        auto& attribs = pti.GetAttribs();
        auto ufx = attribs[PIdx::ufx].data();
        auto ufy = attribs[PIdx::ufy].data();
        auto rho_attr = attribs[PIdx::rho_interp].data();
        const Array4<Real>& u = u_lev.array(pti);
        const Array4<Real>& rho = rho_lev.array(pti);

        amrex::ParallelFor(n, [=] AMREX_GPU_DEVICE(int i) noexcept {
            ibm_interpolate_normal_offset(p_ptr[i], u, rho, delta, ufx[i], ufy[i], rho_attr[i]);
        });
    }
    amrex::Gpu::streamSynchronize();
}

void LagrangeParticleContainer::IDF_ReadInterpResults(int lev,
                                                      std::vector<Real>& local_interp_ux,
                                                      std::vector<Real>& local_interp_uy,
                                                      std::vector<Real>& local_interp_rho) {
    // 首先计算本进程粒子总数
    long total_n = 0;
    for (MyParIter pti(*this, lev); pti.isValid(); ++pti) {
        total_n += pti.numParticles();
    }

    local_interp_ux.resize(total_n);
    local_interp_uy.resize(total_n);
    local_interp_rho.resize(total_n);
    int local_idx = 0;

    for (MyParIter pti(*this, lev); pti.isValid(); ++pti) {
        const long n = pti.numParticles();
        if (n == 0)
            continue;

        auto& attribs = pti.GetAttribs();

        amrex::Gpu::PinnedVector<Real> host_ufx(n), host_ufy(n), host_rho(n);
        amrex::Gpu::copyAsync(amrex::Gpu::deviceToHost,
                              attribs[PIdx::ufx].begin(), attribs[PIdx::ufx].begin() + n,
                              host_ufx.begin());
        amrex::Gpu::copyAsync(amrex::Gpu::deviceToHost,
                              attribs[PIdx::ufy].begin(), attribs[PIdx::ufy].begin() + n,
                              host_ufy.begin());
        amrex::Gpu::copyAsync(amrex::Gpu::deviceToHost,
                              attribs[PIdx::rho_interp].begin(), attribs[PIdx::rho_interp].begin() + n,
                              host_rho.begin());
        amrex::Gpu::streamSynchronize();

        for (long i = 0; i < n; ++i) {
            local_interp_ux[local_idx + i] = host_ufx[i];
            local_interp_uy[local_idx + i] = host_ufy[i];
            local_interp_rho[local_idx + i] = host_rho[i];
        }
        local_idx += n;
    }
}

void LagrangeParticleContainer::IDF_WriteForceToParticles(int lev,
                                                          const std::vector<Real>& sol_x,
                                                          const std::vector<Real>& sol_y,
                                                          const std::unordered_map<int, int>& pid_to_global_idx) {
    const Real ib_coeff = IB_weight * dx_min * dx_min;
    int NL = static_cast<int>(sol_x.size());

    // 将力密度数据复制到 GPU 可访问的内存
    amrex::Gpu::DeviceVector<Real> d_sol_x(NL);
    amrex::Gpu::DeviceVector<Real> d_sol_y(NL);
    amrex::Gpu::copyAsync(amrex::Gpu::hostToDevice,
                          sol_x.data(), sol_x.data() + NL,
                          d_sol_x.data());
    amrex::Gpu::copyAsync(amrex::Gpu::hostToDevice,
                          sol_y.data(), sol_y.data() + NL,
                          d_sol_y.data());
    amrex::Gpu::streamSynchronize();
    const Real* d_sol_x_ptr = d_sol_x.data();
    const Real* d_sol_y_ptr = d_sol_y.data();

    for (MyParIter pti(*this, lev); pti.isValid(); ++pti) {
        auto& aos = pti.GetArrayOfStructs();
        const long n = pti.numParticles();
        if (n == 0)
            continue;

        auto& attribs = pti.GetAttribs();
        auto fx_attr = attribs[PIdx::fx].data();
        auto fy_attr = attribs[PIdx::fy].data();

        // 将粒子数据从 GPU 复制到 CPU 以获取 ID
        using ParticleType = typename LagrangeParticleContainer::ParticleType;
        amrex::Gpu::PinnedVector<ParticleType> host_particles(n);
        amrex::Gpu::copyAsync(amrex::Gpu::deviceToHost,
                              aos().begin(), aos().end(),
                              host_particles.begin());
        amrex::Gpu::streamSynchronize();

        // 构建本 tile 的全局索引数组
        amrex::Gpu::PinnedVector<int> h_global_indices(n);
        for (long i = 0; i < n; ++i) {
            int pid = host_particles[i].id();
            auto it = pid_to_global_idx.find(pid);
            if (it != pid_to_global_idx.end()) {
                h_global_indices[i] = it->second;
            } else {
                amrex::Print() << "[IDF][ERROR] particle ID " << pid
                               << " not found in global mapping!" << std::endl;
                h_global_indices[i] = 0;
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
        Array4<Real> const& Ft = force_lev.array(pti);

        amrex::ParallelFor(n, [=] AMREX_GPU_DEVICE(int i) noexcept {
            ibm_spread_force(p_ptr[i], fx[i], fy[i], Ft, delta);
        });
    }
    amrex::Gpu::streamSynchronize();
}