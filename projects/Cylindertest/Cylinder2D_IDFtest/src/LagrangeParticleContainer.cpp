#include "LagrangeParticleContainer.H"
#include "D2Q9.H"
#include "Kernels.H"

#include <AMReX_ParmParse.H>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <ios>
#include <numeric>
#include <ostream>
#include <vector>
using namespace amrex;

namespace {

struct GlobalForceCoefficients {
    Real fx_sum = 0.0;
    Real fy_sum = 0.0;
    Real cd = 0.0;
    Real cl = 0.0;
};

constexpr char kCdClFile[] = "CdCl.dat";
constexpr char kConvergenceMonitorFile[] = "convergence_monitor.dat";

Real DynamicPressure() {
    return 0.5 * (p0 / cs2) * U0 * U0;
}

Real ReferenceDiameter() {
    return D * dx_0;
}

Real DragReferenceForce() {
    return DynamicPressure() * ReferenceDiameter();
}

GlobalForceCoefficients ComputeGlobalForceCoefficients(const LagrangeParticleContainer& pc) {
    using SPType = typename LagrangeParticleContainer::SuperParticleType;

    auto fx_sum = amrex::ReduceSum(pc, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
        return p.rdata(PIdx::fx);
    });

    auto fy_sum = amrex::ReduceSum(pc, [=] AMREX_GPU_HOST_DEVICE(const SPType& p) -> ParticleReal {
        return p.rdata(PIdx::fy);
    });

    ParallelDescriptor::ReduceRealSum(fx_sum);
    ParallelDescriptor::ReduceRealSum(fy_sum);

    const Real force_ref = DragReferenceForce();
    GlobalForceCoefficients coeffs;
    coeffs.fx_sum = fx_sum;
    coeffs.fy_sum = fy_sum;
    coeffs.cd = fx_sum / force_ref;
    coeffs.cl = fy_sum / force_ref;
    return coeffs;
}

Real ComputeLinfResidual(const MultiFab& current, const MultiFab* previous) {
    if (previous == nullptr) {
        return std::numeric_limits<Real>::infinity();
    }

    MultiFab diff(current.boxArray(), current.DistributionMap(), current.nComp(), 0);
    MultiFab::Copy(diff, current, 0, 0, current.nComp(), 0);
    MultiFab::Subtract(diff, *previous, 0, 0, current.nComp(), 0);
    return diff.norm0(0, current.nComp(), IntVect(0), false);
}

Real ComputeWindowSlope(const std::vector<int>& steps, const std::vector<Real>& values) {
    const int n = static_cast<int>(steps.size());
    if (n < 2) {
        return 0.0;
    }

    const Real mean_step = std::accumulate(steps.begin(), steps.end(), Real(0.0)) / static_cast<Real>(n);
    const Real mean_value = std::accumulate(values.begin(), values.end(), Real(0.0)) / static_cast<Real>(n);

    Real numerator = 0.0;
    Real denominator = 0.0;
    for (int i = 0; i < n; ++i) {
        const Real dt = static_cast<Real>(steps[i]) - mean_step;
        numerator += dt * (values[i] - mean_value);
        denominator += dt * dt;
    }

    if (denominator <= 0.0) {
        return 0.0;
    }

    return numerator / denominator;
}

} // namespace

void LagrangeParticleContainer::ReadConvergenceParameters() {
    ParmParse pp("lbm");
    pp.query("convergence_min_step", convergence_params_.min_check_step);
    pp.query("convergence_check_interval", convergence_params_.check_interval);
    pp.query("convergence_window_size", convergence_params_.window_size);
    pp.query("convergence_required_windows", convergence_params_.required_consecutive_windows);
    pp.query("convergence_post_hold_steps", convergence_params_.post_steady_hold_steps);
    pp.query("cd_rel_span_tol", convergence_params_.cd_rel_span_tol);
    pp.query("cd_slope_tol", convergence_params_.cd_slope_tol);
    pp.query("cl_abs_mean_tol", convergence_params_.cl_abs_mean_tol);
    pp.query("cl_rms_tol", convergence_params_.cl_rms_tol);
    pp.query("u_residual_tol", convergence_params_.u_residual_tol);
    pp.query("rho_residual_tol", convergence_params_.rho_residual_tol);

    if (ParallelDescriptor::IOProcessor()) {
        amrex::Print() << "[Params] convergence:\n"
                       << "  min_check_step    = " << convergence_params_.min_check_step << "\n"
                       << "  check_interval    = " << convergence_params_.check_interval << "\n"
                       << "  window_size       = " << convergence_params_.window_size << "\n"
                       << "  required_windows  = " << convergence_params_.required_consecutive_windows << "\n"
                       << "  post_hold_steps   = " << convergence_params_.post_steady_hold_steps << "\n"
                       << "  cd_rel_span_tol   = " << convergence_params_.cd_rel_span_tol << "\n"
                       << "  cd_slope_tol      = " << convergence_params_.cd_slope_tol << "\n"
                       << "  cl_abs_mean_tol   = " << convergence_params_.cl_abs_mean_tol << "\n"
                       << "  cl_rms_tol        = " << convergence_params_.cl_rms_tol << "\n"
                       << "  u_residual_tol    = " << convergence_params_.u_residual_tol << "\n"
                       << "  rho_residual_tol  = " << convergence_params_.rho_residual_tol << "\n";
    }
}

LagrangeParticleContainer::ConvergenceStats
LagrangeParticleContainer::BuildConvergenceStats(int lev,
                                                 int step,
                                                 amrex::MultiFab& u_lev,
                                                 amrex::MultiFab& rho_lev) {
    amrex::ignore_unused(lev);

    ConvergenceStats stats;
    stats.step = step;

    const auto coeffs = ComputeGlobalForceCoefficients(*this);
    stats.cd = coeffs.cd;
    stats.cl = coeffs.cl;

    sample_steps_.push_back(step);
    cd_samples_.push_back(stats.cd);
    cl_samples_.push_back(stats.cl);

    const int max_size = std::max(convergence_params_.window_size, 1);
    if (static_cast<int>(sample_steps_.size()) > max_size) {
        sample_steps_.erase(sample_steps_.begin());
        cd_samples_.erase(cd_samples_.begin());
        cl_samples_.erase(cl_samples_.begin());
    }

    stats.u_residual = ComputeLinfResidual(u_lev, prev_u_snapshot_.get());
    stats.rho_residual = ComputeLinfResidual(rho_lev, prev_rho_snapshot_.get());

    if (!prev_u_snapshot_) {
        prev_u_snapshot_ = std::make_unique<MultiFab>(u_lev.boxArray(), u_lev.DistributionMap(), u_lev.nComp(), 0);
    }
    if (!prev_rho_snapshot_) {
        prev_rho_snapshot_ = std::make_unique<MultiFab>(rho_lev.boxArray(), rho_lev.DistributionMap(), rho_lev.nComp(), 0);
    }
    MultiFab::Copy(*prev_u_snapshot_, u_lev, 0, 0, u_lev.nComp(), 0);
    MultiFab::Copy(*prev_rho_snapshot_, rho_lev, 0, 0, rho_lev.nComp(), 0);

    const int nsamples = static_cast<int>(cd_samples_.size());
    if (nsamples < convergence_params_.window_size) {
        return stats;
    }

    const auto cd_minmax = std::minmax_element(cd_samples_.begin(), cd_samples_.end());
    stats.cd_mean = std::accumulate(cd_samples_.begin(), cd_samples_.end(), Real(0.0)) / static_cast<Real>(nsamples);
    stats.cd_rel_span =
        (*cd_minmax.second - *cd_minmax.first) / std::max(std::abs(stats.cd_mean), Real(1.0e-12));
    stats.cd_slope = ComputeWindowSlope(sample_steps_, cd_samples_);

    stats.cl_mean = std::accumulate(cl_samples_.begin(), cl_samples_.end(), Real(0.0)) / static_cast<Real>(nsamples);
    Real cl_sq_sum = 0.0;
    for (Real value : cl_samples_) {
        cl_sq_sum += value * value;
    }
    stats.cl_rms = std::sqrt(cl_sq_sum / static_cast<Real>(nsamples));

    stats.window_ok =
        stats.cd_rel_span <= convergence_params_.cd_rel_span_tol &&
        std::abs(stats.cd_slope) <= convergence_params_.cd_slope_tol &&
        std::abs(stats.cl_mean) <= convergence_params_.cl_abs_mean_tol &&
        stats.cl_rms <= convergence_params_.cl_rms_tol &&
        stats.u_residual <= convergence_params_.u_residual_tol &&
        stats.rho_residual <= convergence_params_.rho_residual_tol;

    if (stats.window_ok) {
        ++consecutive_window_hits_;
    } else {
        consecutive_window_hits_ = 0;
        steady_candidate_active_ = false;
        steady_candidate_start_step_ = -1;
    }

    stats.consecutive_windows = consecutive_window_hits_;

    if (!steady_candidate_active_ &&
        consecutive_window_hits_ >= convergence_params_.required_consecutive_windows) {
        steady_candidate_active_ = true;
        steady_candidate_start_step_ = step;
        if (ParallelDescriptor::IOProcessor()) {
            amrex::Print() << "[Convergence] Steady candidate accepted at step " << step
                           << ", entering hold period of "
                           << convergence_params_.post_steady_hold_steps << " steps." << std::endl;
        }
    }

    stats.candidate_start_step = steady_candidate_start_step_;
    stats.steady_candidate = steady_candidate_active_;

    if (steady_candidate_active_ &&
        step - steady_candidate_start_step_ >= convergence_params_.post_steady_hold_steps) {
        steady_confirmed_ = true;
        stats.steady_confirmed = true;
        final_convergence_stats_ = stats;
        if (ParallelDescriptor::IOProcessor()) {
            amrex::Print() << "[Convergence] Hold completed at step " << step
                           << ", safe to stop." << std::endl;
        }
    }

    return stats;
}

void LagrangeParticleContainer::WriteConvergenceMonitor(const ConvergenceStats& stats) const {
    if (!ParallelDescriptor::IOProcessor()) {
        return;
    }

    const bool need_header = !convergence_monitor_initialized_ &&
                             !std::ifstream(kConvergenceMonitorFile).good();
    std::ofstream file(kConvergenceMonitorFile, need_header ? std::ios::trunc : std::ios::app);
    if (!file.is_open()) {
        std::cerr << "Cannot open the file: " << kConvergenceMonitorFile << std::endl;
        std::exit(1);
    }

    if (need_header) {
        file << "# check_interval=" << convergence_params_.check_interval
             << " window_size=" << convergence_params_.window_size
             << " required_windows=" << convergence_params_.required_consecutive_windows
             << " post_hold_steps=" << convergence_params_.post_steady_hold_steps << "\n";
        file << "# cd_rel_span_tol=" << convergence_params_.cd_rel_span_tol
             << " cd_slope_tol=" << convergence_params_.cd_slope_tol
             << " cl_abs_mean_tol=" << convergence_params_.cl_abs_mean_tol
             << " cl_rms_tol=" << convergence_params_.cl_rms_tol
             << " u_residual_tol=" << convergence_params_.u_residual_tol
             << " rho_residual_tol=" << convergence_params_.rho_residual_tol << "\n";
        file << "# step\tCd\tCl\tCd_mean\tCd_span_rel\tCd_slope\tCl_mean\tCl_rms\tu_residual\t"
                "rho_residual\twindow_ok\tsteady_candidate\tsteady_confirmed\n";
    }

    file << stats.step << "\t" << stats.cd << "\t" << stats.cl << "\t"
         << stats.cd_mean << "\t" << stats.cd_rel_span << "\t" << stats.cd_slope << "\t"
         << stats.cl_mean << "\t" << stats.cl_rms << "\t"
         << stats.u_residual << "\t" << stats.rho_residual << "\t"
         << (stats.window_ok ? 1 : 0) << "\t"
         << (stats.steady_candidate ? 1 : 0) << "\t"
         << (stats.steady_confirmed ? 1 : 0) << "\n";

    convergence_monitor_initialized_ = true;
}

void LagrangeParticleContainer::WriteSteadySummaryHeader(std::ostream& os) const {
    if (!steady_confirmed_) {
        return;
    }

    os << "# steady_stop_step      = " << final_convergence_stats_.step << "\n";
    os << "# steady_candidate_step = " << final_convergence_stats_.candidate_start_step << "\n";
    os << "# min_check_step        = " << convergence_params_.min_check_step << "\n";
    os << "# check_interval        = " << convergence_params_.check_interval << "\n";
    os << "# window_size           = " << convergence_params_.window_size << "\n";
    os << "# required_windows      = " << convergence_params_.required_consecutive_windows << "\n";
    os << "# post_hold_steps       = " << convergence_params_.post_steady_hold_steps << "\n";
    os << "# cd_rel_span_tol       = " << convergence_params_.cd_rel_span_tol << "\n";
    os << "# cd_slope_tol          = " << convergence_params_.cd_slope_tol << "\n";
    os << "# cl_abs_mean_tol       = " << convergence_params_.cl_abs_mean_tol << "\n";
    os << "# cl_rms_tol            = " << convergence_params_.cl_rms_tol << "\n";
    os << "# u_residual_tol        = " << convergence_params_.u_residual_tol << "\n";
    os << "# rho_residual_tol      = " << convergence_params_.rho_residual_tol << "\n";
    os << "# final_cd              = " << final_convergence_stats_.cd << "\n";
    os << "# final_cl              = " << final_convergence_stats_.cl << "\n";
    os << "# final_cd_mean         = " << final_convergence_stats_.cd_mean << "\n";
    os << "# final_cd_span_rel     = " << final_convergence_stats_.cd_rel_span << "\n";
    os << "# final_cd_slope        = " << final_convergence_stats_.cd_slope << "\n";
    os << "# final_cl_mean         = " << final_convergence_stats_.cl_mean << "\n";
    os << "# final_cl_rms          = " << final_convergence_stats_.cl_rms << "\n";
    os << "# final_u_residual      = " << final_convergence_stats_.u_residual << "\n";
    os << "# final_rho_residual    = " << final_convergence_stats_.rho_residual << "\n";
}

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
        amrex::Print() << "[DEBUG] Step " << step << " iter " << iter
                       << " Particle fx_sum=" << fx_sum << " fy_sum=" << fy_sum
                       << " (Cd=" << fx_sum / DragReferenceForce()
                       << " Cl=" << fy_sum / DragReferenceForce() << ")" << std::endl;
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
    amrex::ignore_unused(lev);
    const auto coeffs = ComputeGlobalForceCoefficients(*this);

    if (ParallelDescriptor::MyProc() == ParallelDescriptor::IOProcessorNumber()) {
        std::ofstream file(kCdClFile, step == 1 ? std::ios::trunc : std::ios::app);

        if (!file.is_open()) {
            std::cerr << "Cannot open the file: " << kCdClFile << std::endl;
            std::exit(1); // 错误退出
        }

        file << step << "\t" << coeffs.cd << "\t" << coeffs.cl << "\n";
        file.close();
    }
}

void LagrangeParticleContainer::WriteParticle(int step) {
    const std::string& pltfile = amrex::Concatenate("particles", step, 5);
    WriteAsciiFile(pltfile);
}

// 稳态判断：基于 Cd 的历史波动评估收敛性
bool LagrangeParticleContainer::EvaluateConvergence(int lev, int step, amrex::MultiFab& u_lev, amrex::MultiFab& rho_lev) {
    if (steady_confirmed_) {
        return true;
    }

    if (step < convergence_params_.min_check_step ||
        convergence_params_.check_interval <= 0 ||
        step % convergence_params_.check_interval != 0) {
        return false;
    }

    ConvergenceStats stats = BuildConvergenceStats(lev, step, u_lev, rho_lev);
    WriteConvergenceMonitor(stats);
    return stats.steady_confirmed;
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
    const Real q_inf = DynamicPressure();

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
            WriteSteadySummaryHeader(cp_file);
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

// 计算圆柱表面局部摩擦系数分布
void LagrangeParticleContainer::ComputeCf(int lev, MultiFab& u_lev, const std::string& filename) {
    const Real delta = Geom(lev).CellSize()[0];
    const Real delta0 = Geom(0).CellSize()[0];

    // 此处需与粒子坐标保持一致，否则角度会被偏移
    const Real center_x = X * delta0;
    const Real center_y = Y * delta0;
    const Real q_inf = DynamicPressure();

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

        const FArrayBox& u_fab = u_lev[pti];
        const Box& ubox = u_lev[pti].box();
        const int ilo = ubox.smallEnd(0);
        const int ihi = ubox.bigEnd(0);
        const int jlo = ubox.smallEnd(1);
        const int jhi = ubox.bigEnd(1);
        const int nx = ihi - ilo + 1;
        const int ny = jhi - jlo + 1;
        const long ncell = static_cast<long>(nx) * static_cast<long>(ny);

        amrex::Gpu::PinnedVector<Real> host_u0(ncell);
        amrex::Gpu::PinnedVector<Real> host_u1(ncell);
        amrex::Gpu::copyAsync(amrex::Gpu::deviceToHost,
                              u_fab.dataPtr(0), u_fab.dataPtr(0) + ncell,
                              host_u0.begin());
        amrex::Gpu::copyAsync(amrex::Gpu::deviceToHost,
                              u_fab.dataPtr(1), u_fab.dataPtr(1) + ncell,
                              host_u1.begin());
        amrex::Gpu::streamSynchronize();

        auto host_vel = [&](int ix, int iy, int comp) -> Real {
            const long idx = static_cast<long>(ix - ilo) + static_cast<long>(iy - jlo) * static_cast<long>(nx);
            return (comp == 0) ? host_u0[idx] : host_u1[idx];
        };

        for (long i = 0; i < n; ++i) {
            const Real px = host_particles[i].pos(0);
            const Real py = host_particles[i].pos(1);
            const Real theta = std::atan2(py - center_y, px - center_x);

            // 计算壁面法向和切向单位向量
            const Real nx = std::cos(theta);
            const Real ny = std::sin(theta);
            const Real tx = ny; // 切向向量（顺时针90度）
            const Real ty = -nx;

            // 在离壁面不同距离处采样速度
            const Real d1 = 2 * delta; // 近壁面采样点
            const Real d2 = 3 * delta; // 远壁面采样点

            // 计算采样点坐标
            const Real x1 = px + d1 * nx; // 物理坐标
            const Real y1 = py + d1 * ny;
            const Real x2 = px + d2 * nx;
            const Real y2 = py + d2 * ny;

            // 双线性插值计算速度
            auto interpolate_velocity = [&](Real x, Real y) -> std::pair<Real, Real> {
                // u 存在 cell center，坐标需减去 0.5 才与(i,j)中心一致
                Real xt = x / delta - 0.5;
                Real yt = y / delta - 0.5;

                // 需要访问(ix,iy)与(ix+1,iy+1)，因此将连续坐标限制在可插值范围内
                xt = std::max(static_cast<Real>(ilo), std::min(xt, static_cast<Real>(ihi) - 1.0e-12));
                yt = std::max(static_cast<Real>(jlo), std::min(yt, static_cast<Real>(jhi) - 1.0e-12));

                int ix = static_cast<int>(std::floor(xt));
                int iy = static_cast<int>(std::floor(yt));
                Real lx = xt - ix;
                Real ly = yt - iy;

                // 双线性插值
                Real u00 = host_vel(ix, iy, 0);
                Real u10 = host_vel(ix + 1, iy, 0);
                Real u01 = host_vel(ix, iy + 1, 0);
                Real u11 = host_vel(ix + 1, iy + 1, 0);

                Real v00 = host_vel(ix, iy, 1);
                Real v10 = host_vel(ix + 1, iy, 1);
                Real v01 = host_vel(ix, iy + 1, 1);
                Real v11 = host_vel(ix + 1, iy + 1, 1);

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

            // 计算切向速度沿法向的梯度（物理长度导数）
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
            WriteSteadySummaryHeader(cf_file);
            cf_file << "# theta(rad)\tCf\n";
            for (const auto& e : entries) {
                cf_file << e[0] << "\t" << e[1] << "\n";
            }
            cf_file.close();

            // 粘性阻力系数：Cd_v = 0.5 * ∮ Cf(θ) sin(θ) dθ
            // 这里采用按 theta 排序后的梯形积分，并补上周期闭合段
            Real cdv = 0.0;
            const int ntheta = static_cast<int>(entries.size());
            if (ntheta >= 2) {
                for (int t = 0; t < ntheta - 1; ++t) {
                    const Real th0 = entries[t][0];
                    const Real th1 = entries[t + 1][0];
                    const Real f0 = entries[t][1] * std::sin(th0);
                    const Real f1 = entries[t + 1][1] * std::sin(th1);
                    cdv += 0.5 * (f0 + f1) * (th1 - th0);
                }

                const Real th_last = entries[ntheta - 1][0];
                const Real th_first = entries[0][0] + 2.0 * PI;
                const Real f_last = entries[ntheta - 1][1] * std::sin(th_last);
                const Real f_first = entries[0][1] * std::sin(entries[0][0]);
                cdv += 0.5 * (f_last + f_first) * (th_first - th_last);
                cdv *= 0.5;
            }

            amrex::Print() << "[Cf] Steady-state Cf written ("
                           << entries.size() << " points), integrated Cd_v = " << cdv << std::endl;
        }
    }
}

// 通过力与压力分解来计算本地摩擦系数
// 物理理论：总力 = 压力力（法向）+ 粘性力（切向）
// 对于圆形IBM，局部法向/切向基底会随 theta 变化。
// 本函数将切向正方向定义为“与 +x 轴夹角不大于 90 度”（t_x >= 0），
// 即在每个 theta 处在两种切向方向中自动选取 x 分量非负的那一个。
// 因此，投影得到的 Cf / Cp 符号取决于局部坐标系的方向约定：
// 同一物理力在不同角度投影到局部基底后，可能与全局 x/y 分量呈现相反符号。
// 这不是数值错误，而是局部极坐标分解的结果。
// 对于圆形IBM，压力力 ~ -Cp * q_inf * normal
// 粘性力 ~ -Cf * q_inf * tangent
// 因此可以反演求 Cf
void LagrangeParticleContainer::ComputeCf_from_force_pressure(int lev, const std::string& filename) {
    const Real delta0 = Geom(0).CellSize()[0];
    const Real center_x = X * delta0;
    const Real center_y = Y * delta0;
    const Real q_inf = DynamicPressure();
    const Real d_phys = ReferenceDiameter();
    const Real ds = ds_r * dx_min;

    // 每个点存储 [theta, Cd_density_x, Cf, Cp]
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

        // 获取粒子力
        amrex::Gpu::PinnedVector<Real> host_fx(n);
        amrex::Gpu::PinnedVector<Real> host_fy(n);

        amrex::Gpu::copyAsync(amrex::Gpu::deviceToHost,
                              attribs[PIdx::fx].begin(),
                              attribs[PIdx::fx].begin() + n,
                              host_fx.begin());
        amrex::Gpu::copyAsync(amrex::Gpu::deviceToHost,
                              attribs[PIdx::fy].begin(),
                              attribs[PIdx::fy].begin() + n,
                              host_fy.begin());
        amrex::Gpu::streamSynchronize();

        for (long i = 0; i < n; ++i) {
            const Real px = host_particles[i].pos(0);
            const Real py = host_particles[i].pos(1);

            // 计算角度
            const Real theta = std::atan2(py - center_y, px - center_x);

            // 法向单位向量（指向内侧）
            const Real nx = -std::cos(theta);
            const Real ny = -std::sin(theta);

            // 切向单位向量：按“与 +x 轴成锐角/直角”约定自动翻转，使 tx >= 0
            Real tx = std::sin(theta);
            Real ty = -std::cos(theta);
            if (tx < 0.0) {
                tx = -tx;
                ty = -ty;
            }

            // 获取粒子的力（IBM核加权后的离散力）
            const Real fx = host_fx[i];
            const Real fy = host_fy[i];

            // 计算力在法向和切向上的分量
            const Real f_normal = fx * nx + fy * ny;
            const Real f_tangent = fx * tx + fy * ty;

            // 使用真实离散弧长 ds = R * dtheta，而不是初设的 ds0。
            // 这样用角度积分重构得到的阻力才能与离散总力保持一致。
            // f_n = Cp * q_inf * ds, f_t = Cf * q_inf * ds
            const Real cp = f_normal / (q_inf * ds);
            const Real cf = f_tangent / (q_inf * ds);

            // x 向局部力系数密度；其角度积分应重构总 Cd
            const Real cd_local = fx / (q_inf * ds);

            local_data.push_back(theta);
            local_data.push_back(cd_local);
            local_data.push_back(cf);
            local_data.push_back(cp);
        }
    }

    // 全局通信：收集所有MPI进程的数据
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

    // IO处理：排序、积分、输出
    if (ParallelDescriptor::IOProcessor()) {
        std::vector<std::array<Real, 4>> entries; // [theta, Cd_density_x, Cf, Cp]
        entries.reserve(total / 4);
        for (int k = 0; k < total / 4; ++k) {
            entries.push_back({gathered[4 * k], gathered[4 * k + 1], gathered[4 * k + 2], gathered[4 * k + 3]});
        }

        // 按theta排序
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) { return a[0] < b[0]; });

        Real cd_local_sum = 0.0;
        for (const auto& e : entries) {
            cd_local_sum += e[1] * ds / d_phys;
        }

        const Real cd_raw = ComputeGlobalForceCoefficients(*this).cd;

        // 计算积分：
        // Cd_v 使用当前切向定义对应的 t_x = |sin(theta)|，即 0.5 * ∮ Cf(θ) * t_x(θ) dθ
        // Cd_p 保持 -0.5 * ∮ Cp(θ) cos(θ) dθ
        Real cdv = 0.0;
        Real cdp = 0.0;
        const int ntheta = static_cast<int>(entries.size());
        if (ntheta >= 2) {
            for (int t = 0; t < ntheta - 1; ++t) {
                const Real th0 = entries[t][0];
                const Real th1 = entries[t + 1][0];
                const Real fv0 = entries[t][2] * std::abs(std::sin(th0));
                const Real fv1 = entries[t + 1][2] * std::abs(std::sin(th1));
                const Real fp0 = entries[t][3] * std::cos(th0);
                const Real fp1 = entries[t + 1][3] * std::cos(th1);

                cdv += 0.5 * (fv0 + fv1) * (th1 - th0);
                cdp -= 0.5 * (fp0 + fp1) * (th1 - th0);
            }

            // 周期闭合
            const Real th_last = entries[ntheta - 1][0];
            const Real th_first = entries[0][0] + 2.0 * PI;
            const Real fv_last = entries[ntheta - 1][2] * std::abs(std::sin(th_last));
            const Real fv_first = entries[0][2] * std::abs(std::sin(entries[0][0]));
            const Real fp_last = entries[ntheta - 1][3] * std::cos(th_last);
            const Real fp_first = entries[0][3] * std::cos(entries[0][0]);

            cdv += 0.5 * (fv_last + fv_first) * (th_first - th_last);
            cdp -= 0.5 * (fp_last + fp_first) * (th_first - th_last);
            cdv *= 0.5;
            cdp *= 0.5;
        }

        // 输出到文件
        std::ofstream cf_file(filename, std::ios::trunc);
        if (cf_file.is_open()) {
            WriteSteadySummaryHeader(cf_file);
            cf_file << "# q_inf               = " << q_inf << "\n";
            cf_file << "# D_phys              = " << d_phys << "\n";
            cf_file << "# ds(actual arc)      = " << ds << "\n";
            cf_file << "# Cd_raw(sum force)   = " << cd_raw << "\n";
            cf_file << "# Cd_local_sum        = " << cd_local_sum << "\n";
            cf_file << "# Cd_reconstructed    = " << (cdp + cdv) << "\n";
            cf_file << "# Cd_p(integral Cp)   = " << cdp << "\n";
            cf_file << "# Cd_v(integral Cf)   = " << cdv << "\n";
            cf_file << "# theta(rad)\tCd_x_density\tCf(from_tangent_force)\tCp(from_normal_force)\n";
            for (const auto& e : entries) {
                cf_file << e[0] << "\t" << e[1] << "\t" << e[2] << "\t" << e[3] << "\n";
            }
            cf_file.close();

            amrex::Print() << "[Cf_force_pressure] Steady-state Cf written to " << filename
                           << " (" << entries.size() << " points, Cd_raw=" << cd_raw
                           << ", Cd_local_sum=" << cd_local_sum
                           << ", Cd_p=" << cdp << ", Cd_v=" << cdv << ")"
                           << std::endl;
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
