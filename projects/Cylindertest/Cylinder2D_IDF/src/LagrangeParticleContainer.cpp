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

void LagrangeParticleContainer::ZeroParticleForce(int lev) {
    for (MyParIter pti(*this, lev); pti.isValid(); ++pti) {
        const long n = pti.numParticles();

        auto& attribs = pti.GetAttribs();
        auto fx = attribs[PIdx::fx].data();
        auto fy = attribs[PIdx::fy].data();

        amrex::ParallelFor(n, [=] AMREX_GPU_DEVICE(int i) noexcept {
            fx[i] = 0.0;
            fy[i] = 0.0;
        });
    }
}

void LagrangeParticleContainer::DebugPrintForceSum(int lev, int step, int iter) {
    using SPType = typename LagrangeParticleContainer::SuperParticleType;

    auto fx_sum = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
        return p.rdata(PIdx::fx);
    });

    auto fy_sum = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
        return p.rdata(PIdx::fy);
    });

    ParallelDescriptor::ReduceRealSum(fx_sum);
    ParallelDescriptor::ReduceRealSum(fy_sum);

    if (ParallelDescriptor::MyProc() == ParallelDescriptor::IOProcessorNumber()) {
        Real m = 0.5 * (p0 / cs2) * U0 * U0 * D * dx_0;
        amrex::Print() << "[DEBUG] Step " << step << " iter " << iter
                       << " Particle fx_sum=" << fx_sum << " fy_sum=" << fy_sum
                       << " (Cd=" << fx_sum / m << " Cl=" << fy_sum / m << ")" << std::endl;
    }
}

#if USE_MDF_TWO_STAGE
void LagrangeParticleContainer::InterpForce(int lev, amrex::MultiFab& rho_lev, amrex::MultiFab& u_lev, amrex::MultiFab& force_lev, amrex::MultiFab& force_delta_lev) {
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
        const Array4<Real>& Ft_delta = force_delta_lev.array(pti);

        amrex::ParallelFor(n, [=] AMREX_GPU_DEVICE(int i) noexcept {
            force_interp_extrap(p_ptr[i], fx[i], fy[i], ufx[i], ufy[i], u, rho, Ft, Ft_delta, delta);
        });
    }
}
#else
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
#endif
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

// 稳态判断：基于 Cd 的历史波动评估收敛性
bool LagrangeParticleContainer::EvaluateConvergence(int lev, int step, amrex::MultiFab& u_lev, amrex::MultiFab& rho_lev) {
    // 近期采样Cd历史与标志（跨多次调用保持）
    static std::vector<Real> cd_history(30);
    static bool steady_reached = false;

    using SPType = typename LagrangeParticleContainer::SuperParticleType;

    // 每100步评估一次收敛：最近30个采样内相对波动<0.1%
    if (!steady_reached && step % 100 == 0) {
        // 计算当前的 Cd 和 Cl
        auto fx = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
            return p.rdata(PIdx::fx);
        });

        auto fy = amrex::ReduceSum(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
            return p.rdata(PIdx::fy);
        });

        ParallelDescriptor::ReduceRealSum(fx);
        ParallelDescriptor::ReduceRealSum(fy);

        // 归一化为 Cd 和 Cl
        Real m = 0.5 * (p0 / cs2) * U0 * U0 * D * dx_0;
        Real cd_value = fx / m;
        Real cl_value = fy / m;

        // 存储 Cd 历史用于收敛判断
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

    return steady_reached;
}

// 计算圆柱表面压力系数分布（在距离壁面1.5Δx处采样）
void LagrangeParticleContainer::ComputeCp(int lev, MultiFab& rho_lev, const std::string& filename) {

    Interpolate_normal_offset_Rho(lev, rho_lev);

    // 调试：检查插值后的密度范围
    // using SPType = typename LagrangeParticleContainer::SuperParticleType;
    // auto rho_min = amrex::ReduceMin(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
    //     return p.rdata(PIdx::rho_interp);
    // });
    // auto rho_max = amrex::ReduceMax(*this, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
    //     return p.rdata(PIdx::rho_interp);
    // });
    // ParallelDescriptor::ReduceRealMin(rho_min);
    // ParallelDescriptor::ReduceRealMax(rho_max);
    // if (ParallelDescriptor::IOProcessor()) {
    //     amrex::Print() << "[DEBUG] Interpolated rho range: [" << rho_min << ", " << rho_max << "]" << std::endl;
    // }

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

        std::ofstream cp_file(filename, std::ios::trunc);
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

// 重载2：只插值密度
void LagrangeParticleContainer::Interpolate_normal_offset_Rho(int lev,
                                                              MultiFab& rho_lev) {
    const Real delta = Geom(lev).CellSize()[0]; // 当前level的网格间距（物理单位）
    const Real delta0 = Geom(0).CellSize()[0];  // level 0的网格间距（物理单位）

    for (MyParIter pti(*this, lev); pti.isValid(); ++pti) {
        auto& particles = pti.GetArrayOfStructs();
        auto p_ptr = particles().data();
        const long n = pti.numParticles();

        auto& attribs = pti.GetAttribs();
        auto rho_attr = attribs[PIdx::rho_interp].data();
        const Array4<Real>& rho = rho_lev.array(pti);

        // 只插值密度，u 传递 nullptr
        amrex::ParallelFor(n, [=] AMREX_GPU_DEVICE(int i) noexcept {
            interpolate_normal_offset(p_ptr[i], nullptr, &rho, delta, delta0, nullptr, nullptr, &rho_attr[i]);
        });
    }
    amrex::Gpu::streamSynchronize();
}

// 重载3：插值速度和密度（原有接口）
// void LagrangeParticleContainer::Interpolate_normal_offset(int lev,
//                                                           MultiFab& u_lev,
//                                                           MultiFab& rho_lev) {
//     const Real delta = Geom(lev).CellSize()[0];

//     for (MyParIter pti(*this, lev); pti.isValid(); ++pti) {
//         auto& particles = pti.GetArrayOfStructs();
//         auto p_ptr = particles().data();
//         const long n = pti.numParticles();

//         auto& attribs = pti.GetAttribs();
//         auto ufx = attribs[PIdx::ufx].data();
//         auto ufy = attribs[PIdx::ufy].data();
//         auto rho_attr = attribs[PIdx::rho_interp].data();
//         const Array4<Real>& u = u_lev.array(pti);
//         const Array4<Real>& rho = rho_lev.array(pti);

//         // 插值全部：速度 + 密度
//         amrex::ParallelFor(n, [=] AMREX_GPU_DEVICE(int i) noexcept {
//             interpolate_normal_offset(p_ptr[i], &u, &rho, delta,
//                                       &ufx[i], &ufy[i], &rho_attr[i]);
//         });
//     }
//     amrex::Gpu::streamSynchronize();
// }

void LagrangeParticleContainer::IDF_ReadInterpResults(int lev,
                                                      std::vector<Real>& local_interp_ux,
                                                      std::vector<Real>& local_interp_uy,
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
                                                          int idf_NL_global) {
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

        // 方案 C: 使用粒子ID直接计算全局索引（无需映射表）
        // 粒子ID从1开始，全局索引 = ID - 1
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
            int global_idx = pid - 1; // 方案 C: 直接计算
            if (global_idx >= 0 && global_idx < idf_NL_global) {
                h_global_indices[i] = global_idx;
            } else {
                amrex::Print() << "[IDF][ERROR] particle ID " << pid
                               << " out of range [1, " << idf_NL_global << "]!" << std::endl;
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

// 计算圆柱表面局部摩擦系数分布
void LagrangeParticleContainer::ComputeCf(int lev, MultiFab& u_lev, const std::string& filename) {
    const Real delta = Geom(lev).CellSize()[0];
    const Real delta0 = Geom(0).CellSize()[0];

    // 此处需与粒子坐标保持一致，否则角度会被偏移
    const Real center_x = X * delta0;
    const Real center_y = Y * delta0;
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
        amrex::Gpu::streamSynchronize();

        const Array4<Real>& u = u_lev.array(pti);

        for (long i = 0; i < n; ++i) {
            const Real px = host_particles[i].pos(0);
            const Real py = host_particles[i].pos(1);
            const Real theta = std::atan2(py - center_y, px - center_x);

            // 计算壁面法向和切向单位向量
            const Real nx = std::cos(theta);
            const Real ny = std::sin(theta);
            const Real tx = -ny; // 切向向量（逆时针90度）
            const Real ty = nx;

            // 在离壁面不同距离处采样速度
            const Real d1 = 1.5 * delta; // 近壁面采样点
            const Real d2 = 2.5 * delta; // 远壁面采样点

            // 计算采样点坐标
            const Real x1 = px + d1 * nx;
            const Real y1 = py + d1 * ny;
            const Real x2 = px + d2 * nx;
            const Real y2 = py + d2 * ny;

            // 双线性插值计算速度
            auto interpolate_velocity = [&](Real x, Real y) -> std::pair<Real, Real> {
                Real xt = x / delta;
                Real yt = y / delta;
                int ix = static_cast<int>(std::floor(xt));
                int iy = static_cast<int>(std::floor(yt));
                Real lx = xt - ix;
                Real ly = yt - iy;

                // 双线性插值
                Real u00 = u(ix, iy, 0, 0);
                Real u10 = u(ix + 1, iy, 0, 0);
                Real u01 = u(ix, iy + 1, 0, 0);
                Real u11 = u(ix + 1, iy + 1, 0, 0);

                Real v00 = u(ix, iy, 0, 1);
                Real v10 = u(ix + 1, iy, 0, 1);
                Real v01 = u(ix, iy + 1, 0, 1);
                Real v11 = u(ix + 1, iy + 1, 0, 1);

                Real u_interp = (1 - lx) * (1 - ly) * u00 + lx * (1 - ly) * u10 +
                                (1 - lx) * ly * u01 + lx * ly * u11;
                Real v_interp = (1 - lx) * (1 - ly) * v00 + lx * (1 - ly) * v10 +
                                (1 - lx) * ly * v01 + lx * ly * v11;

                return {u_interp, v_interp};
            };

            // 计算两个采样点的速度
            auto [u1, v1] = interpolate_velocity(x1, y1);
            auto [u2, v2] = interpolate_velocity(x2, y2);

            // 计算切向速度
            Real ut1 = u1 * tx + v1 * ty; // 点1的切向速度
            Real ut2 = u2 * tx + v2 * ty; // 点2的切向速度

            // 计算切向速度沿法向的梯度
            Real dut_dn = (ut2 - ut1) / (d2 - d1);

            // 计算剪切应力
            Real rho = p0 / cs2;              // 使用参考密度
            Real tau_w = rho * mv_0 * dut_dn; // 切向剪切应力

            // 计算局部摩擦系数
            Real cf = tau_w / q_inf;

            local_data.push_back(theta);
            local_data.push_back(cf);
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

        std::ofstream cf_file(filename, std::ios::trunc);
        if (cf_file.is_open()) {
            cf_file << "# theta(rad)\tCf\n";
            for (const auto& e : entries) {
                cf_file << e[0] << "\t" << e[1] << "\n";
            }
            cf_file.close();
            amrex::Print() << "[Cf] Steady-state Cf written ("
                           << entries.size() << " points)" << std::endl;
        }
    }
}