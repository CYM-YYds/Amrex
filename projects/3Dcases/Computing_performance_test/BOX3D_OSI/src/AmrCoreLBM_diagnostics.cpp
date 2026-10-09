#include <AMReX_PlotFileUtil.H>
#include "AmrCoreLBM.H"
#include "AmrCoreLBM_detail.H"

#include <AMReX_MultiFabUtil.H>
#include <AMReX_ParIter.H>
#include <AMReX_Reduce.H>
#include <AMReX_Utility.H>
#include <AMReX_VisMF.H>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

using namespace amrex;
using namespace Box3dDetail;

// 显式验证、诊断与收敛监测。

std::array<Real, 3> AmrCoreLBM::MeasureLevel0VelocityChange(
    const MultiFab& previous) const {
    const MultiFab& velocity_0 = velocity.at(0);
    MultiFab velocity_delta(
        velocity_0.boxArray(), velocity_0.DistributionMap(),
        AMREX_SPACEDIM, 0);
    MultiFab::Copy(
        velocity_delta, velocity_0, 0, 0, AMREX_SPACEDIM, 0);
    MultiFab::Subtract(
        velocity_delta, previous, 0, 0, AMREX_SPACEDIM, 0);
    Gpu::synchronize();

    Real numerator_sq = 0.0;
    Real denominator_sq = 0.0;
    for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
        const Real diff_l2 = velocity_delta.norm2(dir, 1);
        const Real velocity_l2 = velocity_0.norm2(dir, 1);
        numerator_sq += diff_l2 * diff_l2;
        denominator_sq += velocity_l2 * velocity_l2;
    }

    const Real delta_velocity_l2 = std::sqrt(numerator_sq);
    const Real velocity_l2 = std::sqrt(denominator_sq);
    return {delta_velocity_l2 /
                amrex::max(velocity_l2, std::numeric_limits<Real>::min()),
            velocity_l2, delta_velocity_l2};
}

void AmrCoreLBM::InitializeConvergence() {
    if (!convergence_.enabled) {
        return;
    }

    ComputeMacro();
    Gpu::synchronize();

    const MultiFab& velocity_0 = velocity.at(0);
    convergence_.previous_check_velocity = std::make_unique<MultiFab>(
        velocity_0.boxArray(), velocity_0.DistributionMap(),
        AMREX_SPACEDIM, 0);
    convergence_.previous_trend_velocity = std::make_unique<MultiFab>(
        velocity_0.boxArray(), velocity_0.DistributionMap(),
        AMREX_SPACEDIM, 0);
    MultiFab::Copy(*convergence_.previous_check_velocity, velocity_0, 0, 0,
                   AMREX_SPACEDIM, 0);
    MultiFab::Copy(*convergence_.previous_trend_velocity, velocity_0, 0, 0,
                   AMREX_SPACEDIM, 0);
    convergence_.streak = 0;
}

bool AmrCoreLBM::CheckConvergence(int step) {
    if (!convergence_.enabled) {
        return false;
    }

    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        convergence_.previous_check_velocity &&
            convergence_.previous_trend_velocity,
        "InitializeConvergence must be called before CheckConvergence");
    const int elapsed_steps = step - params_.begin_step;
    const bool convergence_sample = elapsed_steps % convergence_.check_int == 0;
    const bool trend_sample = elapsed_steps % convergence_.trend_int == 0;
    if (!convergence_sample && !trend_sample) {
        return false;
    }

    ComputeMacro();
    Gpu::synchronize();
    const MultiFab& velocity_0 = velocity.at(0);
    if (trend_sample) {
        const auto values =
            MeasureLevel0VelocityChange(*convergence_.previous_trend_velocity);
        amrex::Print() << "CONVERGENCE_TREND step=" << step
                       << " interval=" << convergence_.trend_int
                       << " velocity_l2_relative=" << values[0]
                       << " velocity_l2=" << values[1]
                       << " delta_velocity_l2=" << values[2] << '\n';
        MultiFab::Copy(*convergence_.previous_trend_velocity, velocity_0, 0, 0,
                       AMREX_SPACEDIM, 0);
    }

    if (!convergence_sample) {
        return false;
    }

    const auto values =
        MeasureLevel0VelocityChange(*convergence_.previous_check_velocity);
    convergence_.streak = values[0] < convergence_.tolerance
                              ? convergence_.streak + 1
                              : 0;
    const bool converged = convergence_.streak >= convergence_.required;
    amrex::Print() << "CONVERGENCE step=" << step
                   << " interval=" << convergence_.check_int
                   << " velocity_l2_relative=" << values[0]
                   << " velocity_l2=" << values[1]
                   << " delta_velocity_l2=" << values[2]
                   << " tolerance=" << convergence_.tolerance
                   << " consecutive=" << convergence_.streak
                   << '/' << convergence_.required
                   << " converged=" << converged << '\n';
    MultiFab::Copy(*convergence_.previous_check_velocity, velocity_0, 0, 0,
                   AMREX_SPACEDIM, 0);
    return converged;
}

void AmrCoreLBM::ProbeAverageInterfaceLevel(int lev, const char* stage) {
    // 首次界面平均的入口和出口分别导出粗层 valid 及区域掩码。
    constexpr int ex[Q] = {
        0, 0, 0, -1, 1, 0, 0, -1, 1, -1, 1, 0, 0, -1,
        1, 0, 0, -1, 1, 1, -1, 1, -1, 1, -1, 1, -1};
    constexpr int ey[Q] = {
        0, 1, -1, 0, 0, 0, 0, 1, 1, -1, -1, 1, -1, 0,
        0, 1, -1, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1};
    constexpr int ez[Q] = {
        0, 0, 0, 0, 0, 1, -1, 0, 0, 0, 0, 1, 1, 1,
        1, -1, -1, -1, -1, 1, 1, 1, 1, -1, -1, -1, -1};
    const MultiFab& state = stream_mode == 1 ? osi_state.at(lev) : f_old.at(lev);
    const std::string stem = "average_probe_" + std::string(stage) + "_mode" + std::to_string(stream_mode) + "_rank" + std::to_string(ParallelDescriptor::MyProc());
    std::ofstream meta(stem + ".meta");
    std::ofstream binary(stem + ".bin", std::ios::binary);
    std::ofstream masks(stem + ".mask", std::ios::binary);
    AMREX_ALWAYS_ASSERT(meta && binary && masks);
    Gpu::synchronize();
    Long local_cells = 0;
    for (MFIter mfi(state, false); mfi.isValid(); ++mfi) {
        const Box valid = mfi.validbox();
        const FArrayBox& device = state[mfi];
        FArrayBox host(device.box(), Q, The_Pinned_Arena());
        Gpu::dtoh_memcpy(host.dataPtr(), device.dataPtr(), host.nBytes());
        const auto& device_covered = covered_mask.at(lev)[mfi];
        const auto& device_interface = interface_mask.at(lev)[mfi];
        IArrayBox host_covered(device_covered.box(), 1, The_Pinned_Arena());
        IArrayBox host_interface(device_interface.box(), 1, The_Pinned_Arena());
        Gpu::dtoh_memcpy(host_covered.dataPtr(), device_covered.dataPtr(),
                         host_covered.nBytes());
        Gpu::dtoh_memcpy(host_interface.dataPtr(), device_interface.dataPtr(),
                         host_interface.nBytes());
        const Box ring = device.box();
        const auto [fab, shift] = OSI::MakeOsiFabContext(ring, osi_phase.at(lev));
        const auto data = host.const_array();
        const auto covered = host_covered.const_array();
        const auto interface = host_interface.const_array();
        meta << mfi.index();
        for (int d = 0; d < 3; ++d) {
            meta << ' ' << valid.smallEnd(d) << ' ' << valid.bigEnd(d);
        }
        meta << '\n';
        local_cells += valid.numPts();
        std::vector<unsigned char> flags;
        flags.reserve(static_cast<std::size_t>(valid.numPts()) * 2);
        for (int k = valid.smallEnd(2); k <= valid.bigEnd(2); ++k) {
            for (int j = valid.smallEnd(1); j <= valid.bigEnd(1); ++j) {
                for (int i = valid.smallEnd(0); i <= valid.bigEnd(0); ++i) {
                    flags.push_back(static_cast<unsigned char>(covered(i, j, k)));
                    flags.push_back(static_cast<unsigned char>(interface(i, j, k)));
                }
            }
        }
        masks.write(reinterpret_cast<const char*>(flags.data()),
                    static_cast<std::streamsize>(flags.size()));
        std::vector<Real> values;
        values.reserve(static_cast<std::size_t>(valid.numPts()));
        for (int q = 0; q < Q; ++q) {
            values.clear();
            for (int k = valid.smallEnd(2); k <= valid.bigEnd(2); ++k) {
                for (int j = valid.smallEnd(1); j <= valid.bigEnd(1); ++j) {
                    for (int i = valid.smallEnd(0); i <= valid.bigEnd(0); ++i) {
                        IntVect address(AMREX_D_DECL(i, j, k));
                        if (stream_mode == 1) {
                            const auto raw = OSI::osi_address(
                                {i, j, k}, {ex[q], ey[q], ez[q]}, fab, shift);
                            address = IntVect(AMREX_D_DECL(raw.x, raw.y, raw.z));
                        }
                        values.push_back(data(address[0], address[1], address[2], q));
                    }
                }
            }
            binary.write(reinterpret_cast<const char*>(values.data()),
                         static_cast<std::streamsize>(values.size() * sizeof(Real)));
        }
    }
    AMREX_ALWAYS_ASSERT(meta.good() && binary.good() && masks.good());
    amrex::AllPrint() << "average_probe: stage=" << stage
                      << " mode=" << stream_mode
                      << " lev=0 first_average=1 local_cells=" << local_cells
                      << " stem=" << stem << '\n';
}

void AmrCoreLBM::ProbeFillGhostLevel(int lev) {
    // 首次重网格后，保存真正由粗层插值写入的 fine ghost，不含同层覆盖区。
    constexpr int ex[Q] = {
        0, 0, 0, -1, 1, 0, 0, -1, 1, -1, 1, 0, 0, -1,
        1, 0, 0, -1, 1, 1, -1, 1, -1, 1, -1, 1, -1};
    constexpr int ey[Q] = {
        0, 1, -1, 0, 0, 0, 0, 1, 1, -1, -1, 1, -1, 0,
        0, 1, -1, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1};
    constexpr int ez[Q] = {
        0, 0, 0, 0, 0, 1, -1, 0, 0, 0, 0, 1, 1, 1,
        1, -1, -1, -1, -1, 1, 1, 1, 1, -1, -1, -1, -1};
    const MultiFab& state = stream_mode == 1 ? osi_state.at(lev) : f_old.at(lev);
    const std::string stem = "ghost_probe_mode" + std::to_string(stream_mode) + "_rank" + std::to_string(ParallelDescriptor::MyProc());
    std::ofstream meta(stem + ".meta");
    std::ofstream binary(stem + ".bin", std::ios::binary);
    AMREX_ALWAYS_ASSERT(meta && binary);
    Long local_cells = 0;
    const auto& work_boxes = interp_direct_fine_boxes.at(lev);
    const auto& fine_indices = interp_direct_fine_index.at(lev);
    Gpu::synchronize();
    for (int n = 0; n < work_boxes.size(); ++n) {
        const int fine_index = fine_indices[n];
        if (state.DistributionMap()[fine_index] != ParallelDescriptor::MyProc()) {
            continue;
        }
        const Box& work = work_boxes[n];
        const Box& valid = state.boxArray()[fine_index];
        const FArrayBox& device = state[fine_index];
        FArrayBox host(device.box(), Q, The_Pinned_Arena());
        Gpu::dtoh_memcpy(host.dataPtr(), device.dataPtr(), host.nBytes());
        const Box ring = device.box();
        const auto [fab, shift] = OSI::MakeOsiFabContext(ring, osi_phase.at(lev));
        const auto data = host.const_array();
        meta << fine_index;
        for (int d = 0; d < 3; ++d) {
            meta << ' ' << work.smallEnd(d) << ' ' << work.bigEnd(d);
        }
        for (int d = 0; d < 3; ++d) {
            meta << ' ' << valid.smallEnd(d) << ' ' << valid.bigEnd(d);
        }
        meta << '\n';
        local_cells += work.numPts();
        std::vector<Real> values;
        values.reserve(static_cast<std::size_t>(work.numPts()) * Q);
        for (int q = 0; q < Q; ++q) {
            for (int k = work.smallEnd(2); k <= work.bigEnd(2); ++k) {
                for (int j = work.smallEnd(1); j <= work.bigEnd(1); ++j) {
                    for (int i = work.smallEnd(0); i <= work.bigEnd(0); ++i) {
                        IntVect address(AMREX_D_DECL(i, j, k));
                        if (stream_mode == 1) {
                            const auto raw = OSI::osi_address(
                                {i, j, k}, {ex[q], ey[q], ez[q]}, fab, shift);
                            address = IntVect(AMREX_D_DECL(raw.x, raw.y, raw.z));
                        }
                        values.push_back(data(address[0], address[1],
                                              address[2], q));
                    }
                }
            }
        }
        binary.write(reinterpret_cast<const char*>(values.data()),
                     static_cast<std::streamsize>(values.size() * sizeof(Real)));
    }
    AMREX_ALWAYS_ASSERT(meta.good() && binary.good());
    amrex::AllPrint() << "ghost_probe: mode=" << stream_mode
                      << " lev=1 fill_count=1 boxes=" << work_boxes.size()
                      << " local_cells=" << local_cells
                      << " stem=" << stem << '\n';
}

void AmrCoreLBM::ValidateConfiguration() const {
    const char* run_mode_name =
        run_mode == RunMode::CanonicalAB
            ? "AB-production"
            : (run_mode == RunMode::OsiProduction ? "OSI-production"
                                                  : "OSI-lockstep");
    if (stream_mode != 1) {
        amrex::Print() << "[LBM validation] run_mode=" << run_mode_name
                       << " full_ddf_arrays="
                       << (canonicalStateEnabled() ? 2 : 1) << '\n';
        return;
    }

    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        max_level <= max_ref_level && finest_level <= max_ref_level,
        "OSI AMR level exceeds the number of levels compiled into the LBM model");
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        collide_mode == 1,
        "OSI currently supports only lbm.collide_mode=1");
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        !osi_mpi_device_direct || osi_mpi_direct,
        "lbm.osi_mpi_device_direct requires lbm.osi_mpi_direct=1");
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        !osi_mpi_device_direct || ParallelDescriptor::UseGpuAwareMpi(),
        "lbm.osi_mpi_device_direct requires AMReX GPU-aware MPI support; "
        "set amrex.use_gpu_aware_mpi=1 only when the MPI implementation supports device pointers");
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        !osi_mpi_async_staging ||
            (osi_mpi_direct && !osi_mpi_device_direct &&
             Gpu::Device::numGpuStreams() > 1),
        "lbm.osi_mpi_async_staging requires host-staged lbm.osi_mpi_direct=1 "
        "and at least two AMReX GPU streams");
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        osi_mpi_pipeline_chunk_bytes == 0 ||
            (osi_mpi_direct && !osi_mpi_device_direct &&
             !osi_mpi_async_staging && Gpu::Device::numGpuStreams() > 1),
        "lbm.osi_mpi_pipeline_chunk_bytes requires host-staged "
        "lbm.osi_mpi_direct=1, async_staging=0, and at least two GPU streams");
    if (max_level > 1) {
        amrex::Print()
            << "[OSI validation warning] More than two AMR levels are enabled. "
               "Finite-state/output checks are active, but four-level OSI/A-B "
               "cellwise equivalence has not yet met the 1e-12 criterion.\n";
    }
    for (int lev = 0; lev <= finest_level; ++lev) {
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            osi_state.at(lev).isDefined(),
            "OSI state was not initialized for an active AMR level");
    }

    bool all_periodic = true;
    for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
        all_periodic = all_periodic && Geom(0).isPeriodic(dir);
    }

    amrex::Print() << (all_periodic ? "[OSI periodic] ranks=" : "[OSI boundary] ranks=")
                   << amrex::ParallelDescriptor::NProcs()
                   << " active_levels=" << finest_level + 1
                   << " run_mode=" << run_mode_name
                   << " seed_pattern=" << (osi_verification_pattern ? 1 : 0)
                   << " ab_check=" << (osiReferenceEnabled() ? 1 : 0)
                   << " full_ddf_arrays="
                   << (osiReferenceEnabled() ? 3 : 1)
                   << " full_q_communication=1"
                   << " mpi_transport="
                   << (osi_mpi_device_direct ? "device" : "host-staging")
                   << " mpi_async_staging=" << (osi_mpi_async_staging ? 1 : 0)
                   << " mpi_pipeline_chunk_bytes="
                   << osi_mpi_pipeline_chunk_bytes
                   << " grown-fab overlap synchronization enabled\n";

    for (int lev = 0; lev <= finest_level; ++lev) {
        amrex::Long state_values = 0;
        const BoxArray& state_ba = osi_state[lev].boxArray();
        const IntVect state_ng = osi_state[lev].nGrowVect();
        for (int ibox = 0; ibox < state_ba.size(); ++ibox) {
            state_values +=
                amrex::grow(state_ba[ibox], state_ng).numPts() * Q;
        }
        amrex::Print() << "[OSI level] level=" << lev
                       << " boxes=" << state_ba.size()
                       << " state_values=" << state_values
                       << " sync_values=0 (按调用临时分配)"
                       << " ring_ngrow=" << state_ng[0]
                       << " phase=" << osi_phase[lev] << '\n';
    }
}

void AmrCoreLBM::ValidateInitializedState(const char* context) {
    for (int lev = 0; lev <= finest_level; ++lev) {
        const MultiFab& state =
            stream_mode == 1 ? osi_state.at(lev) : f_old.at(lev);
        const bool has_fine = lev < finest_level;
        MultiFab decoded;
        if (stream_mode == 1) {
            decoded.define(state.boxArray(), state.DistributionMap(),
                           Q, state.nGrowVect());
            DecodeOsiValid(state, osi_phase.at(lev), decoded);
        }
        MultiFab active_scalar(
            state.boxArray(), state.DistributionMap(), 1, 0);

        // Deep-covered coarse cells are deliberately skipped by Collide/Stream
        // and may contain stale or undefined values after a layout change.
        // Validation must follow the same ownership rule as the solver instead
        // of treating those inactive cells as part of the numerical solution.
        // Physical validity is defined on uncovered cells only.  Interface
        // covered cells are an AMR coupling diagnostic, not solver-owned
        // physical cells and may legitimately be stale after a regrid.
        const auto active_stats =
            [&](const MultiFab& source, int comp, Real inactive_value) {
                if (!has_fine) {
                    return GpuArray<Real, 3>{source.min(comp, 0),
                                             source.max(comp, 0),
                                             source.sum(comp, 0)};
                }
                for (MFIter mfi(source, false); mfi.isValid(); ++mfi) {
                    const Box bx = mfi.validbox();
                    const auto src = source.const_array(mfi);
                    const auto dst = active_scalar.array(mfi);
                    const auto covered =
                        covered_mask.at(lev).const_array(mfi);
                    amrex::ParallelFor(
                        bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                            dst(i, j, k) = (covered(i, j, k) == 0)
                                               ? src(i, j, k, comp)
                                               : inactive_value;
                        });
                }
                return GpuArray<Real, 3>{active_scalar.min(0, 0),
                                         active_scalar.max(0, 0),
                                         active_scalar.sum(0, 0)};
            };
        Real ddf_min = std::numeric_limits<Real>::max();
        Real ddf_max = std::numeric_limits<Real>::lowest();

        for (int q = 0; q < Q; ++q) {
            const MultiFab& canonical = stream_mode == 1 ? decoded : state;
            const auto stats = active_stats(canonical, q, Real(0.0));
            const Real q_min = stats[0];
            const Real q_max = stats[1];
            const Real q_sum = stats[2];
            AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
                std::isfinite(q_min) && std::isfinite(q_max) &&
                    std::isfinite(q_sum) && q_min <= q_max,
                "Initialized valid DDF contains a NaN or infinity");
            ddf_min = amrex::min(ddf_min, q_min);
            ddf_max = amrex::max(ddf_max, q_max);
        }

        ComputeMacroLevel(lev);
        const auto rho_stats =
            active_stats(density.at(lev), 0, Real(1.0));
        const Real rho_min = rho_stats[0];
        const Real rho_max = rho_stats[1];
        const Real rho_sum = rho_stats[2];
        Real velocity_linf = 0.0;
        for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
            const auto velocity_stats =
                active_stats(velocity.at(lev), dir, Real(0.0));
            velocity_linf = amrex::max(
                velocity_linf,
                amrex::max(std::abs(velocity_stats[0]),
                           std::abs(velocity_stats[1])));
        }
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            std::isfinite(rho_min) && std::isfinite(rho_max) &&
                std::isfinite(rho_sum) && rho_min <= rho_max &&
                std::isfinite(velocity_linf) && rho_min > 0.0,
            "Initialized macroscopic state is invalid");
        amrex::Print() << '[' << context << "] level=" << lev
                       << " phase="
                       << (stream_mode == 1 ? osi_phase.at(lev) : 0)
                       << " ddf_min=" << ddf_min
                       << " ddf_max=" << ddf_max
                       << " rho_min=" << rho_min
                       << " rho_max=" << rho_max
                       << " velocity_linf=" << velocity_linf << '\n';
    }
}

void AmrCoreLBM::CheckOsiReferenceLevel0(int step, const char* stage) {
    if (!osiReferenceEnabled()) {
        return;
    }
    amrex::Print() << "osi_ab_pointwise_begin: step=" << step
                   << " stage=" << stage
                   << " finest_level=" << finest_level
                   << " phase0=" << osi_phase.at(0) << '\n';
    CompareOsiReferenceStage(0, stage, f_old.at(0), 0);
    for (int lev = 1; lev <= finest_level; ++lev) {
        const MultiFab& reference = f_old.at(lev);
        const bool nonfinite = reference.contains_nan(0, Q, 0) ||
                               reference.contains_inf(0, Q, 0);
        amrex::Print() << "osi_ab_fine_reference: step=" << step
                       << " stage=" << stage << " lev=" << lev
                       << " finite=" << (nonfinite ? 0 : 1) << '\n';
        AMREX_ALWAYS_ASSERT(!nonfinite);
        CompareOsiReferenceStage(lev, stage, reference, 0);
    }

    // 独立从设备拷回两份状态，逐 cell、逐 q 核对 level 0 全部 valid。
    // 不使用 MultiFab 范数，以便检查已有 GPU 归约诊断的可信度。
    constexpr int ex[Q] = {
        0, 0, 0, -1, 1, 0, 0, -1, 1, -1, 1, 0, 0, -1,
        1, 0, 0, -1, 1, 1, -1, 1, -1, 1, -1, 1, -1};
    constexpr int ey[Q] = {
        0, 1, -1, 0, 0, 0, 0, 1, 1, -1, -1, 1, -1, 0,
        0, 1, -1, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1};
    constexpr int ez[Q] = {
        0, 0, 0, 0, 0, 1, -1, 0, 0, 0, 0, 1, 1, 1,
        1, -1, -1, -1, -1, 1, 1, 1, 1, -1, -1, -1, -1};
    const auto& reference = f_old.at(0);
    const auto& state = osi_state.at(0);
    const auto phase = osi_phase.at(0);
    Long local_values = 0;
    Long local_unequal = 0;
    Long local_above_tolerance = 0;
    Long local_uncovered_values = 0;
    Long local_uncovered_unequal = 0;
    Long local_covered_unequal = 0;
    bool local_nonfinite = false;
    Real local_max_error = 0.0;
    Real local_uncovered_max_error = 0.0;
    Real local_covered_max_error = 0.0;
    IntVect local_max_iv(0);
    int local_max_q = -1;
    Real local_max_ab = 0.0;
    Real local_max_osi = 0.0;
    Gpu::synchronize();
    for (MFIter mfi(reference, false); mfi.isValid(); ++mfi) {
        FArrayBox host_reference(
            reference[mfi].box(), Q, The_Pinned_Arena());
        FArrayBox host_state(
            state[mfi].box(), Q, The_Pinned_Arena());
        Gpu::dtoh_memcpy(host_reference.dataPtr(),
                         reference[mfi].dataPtr(),
                         host_reference.nBytes());
        Gpu::dtoh_memcpy(host_state.dataPtr(), state[mfi].dataPtr(),
                         host_state.nBytes());
        const bool has_fine = finest_level > 0;
        std::unique_ptr<IArrayBox> host_covered;
        if (has_fine) {
            const auto& device_covered = covered_mask.at(0)[mfi];
            host_covered = std::make_unique<IArrayBox>(
                device_covered.box(), 1, The_Pinned_Arena());
            Gpu::dtoh_memcpy(host_covered->dataPtr(),
                             device_covered.dataPtr(),
                             host_covered->nBytes());
        }
        const auto ab = host_reference.const_array();
        const auto osi = host_state.const_array();
        const Box bx = mfi.validbox();
        const Box ring = state[mfi].box();
        const auto [fab, shift] = OSI::MakeOsiFabContext(ring, phase);
        for (int q = 0; q < Q; ++q) {
            for (int k = bx.smallEnd(2); k <= bx.bigEnd(2); ++k) {
                for (int j = bx.smallEnd(1); j <= bx.bigEnd(1); ++j) {
                    for (int i = bx.smallEnd(0); i <= bx.bigEnd(0); ++i) {
                        const auto raw = OSI::osi_address(
                            {i, j, k}, {ex[q], ey[q], ez[q]}, fab, shift);
                        const Real a = ab(i, j, k, q);
                        const Real b = osi(raw.x, raw.y, raw.z, q);
                        const bool covered = has_fine &&
                                             host_covered->const_array()(i, j, k) != 0;
                        ++local_values;
                        if (!covered) {
                            ++local_uncovered_values;
                        }
                        if (!std::isfinite(a) || !std::isfinite(b)) {
                            local_nonfinite = true;
                        }
                        if (a != b) {
                            ++local_unequal;
                            const Real error = std::abs(a - b);
                            if (covered) {
                                ++local_covered_unequal;
                                if (std::isfinite(error)) {
                                    local_covered_max_error =
                                        amrex::max(local_covered_max_error, error);
                                }
                            } else {
                                ++local_uncovered_unequal;
                                if (std::isfinite(error)) {
                                    local_uncovered_max_error =
                                        amrex::max(local_uncovered_max_error, error);
                                }
                            }
                            if (error > Real(1.0e-12)) {
                                ++local_above_tolerance;
                            }
                            if (std::isfinite(error) &&
                                error > local_max_error) {
                                local_max_error = error;
                                local_max_iv = IntVect(AMREX_D_DECL(i, j, k));
                                local_max_q = q;
                                local_max_ab = a;
                                local_max_osi = b;
                            }
                        }
                    }
                }
            }
        }
    }
    auto output = amrex::AllPrint();
    output.SetPrecision(17);
    output << "osi_ab_level0_pointwise: step=" << step
           << " stage=" << stage
           << " lev=0 phase=" << phase
           << " valid_values=" << local_values
           << " unequal=" << local_unequal
           << " uncovered_values=" << local_uncovered_values
           << " uncovered_unequal=" << local_uncovered_unequal
           << " covered_unequal=" << local_covered_unequal
           << " above_1e-12=" << local_above_tolerance
           << " nonfinite=" << (local_nonfinite ? 1 : 0)
           << " linf=" << local_max_error
           << " uncovered_linf=" << local_uncovered_max_error
           << " covered_linf=" << local_covered_max_error;
    if (local_max_q >= 0) {
        output << " max_iv=" << local_max_iv
               << " q=" << local_max_q
               << " ab=" << local_max_ab
               << " osi=" << local_max_osi;
    }
    output << '\n';
}

void AmrCoreLBM::CheckOsiReferenceUncovered(
    int step, const char* stage) {
    if (!osiReferenceEnabled()) {
        return;
    }
    amrex::Print() << "osi_ab_uncovered_begin: step=" << step
                   << " stage=" << stage
                   << " finest_level=" << finest_level << '\n';
    for (int lev = 0; lev <= finest_level; ++lev) {
        const MultiFab& reference = f_old.at(lev);
        const bool nonfinite = reference.contains_nan(0, Q, 0) ||
                               reference.contains_inf(0, Q, 0);
        amrex::Print() << "osi_ab_uncovered_finite: step=" << step
                       << " stage=" << stage << " lev=" << lev
                       << " finite=" << (nonfinite ? 0 : 1) << '\n';
        AMREX_ALWAYS_ASSERT(!nonfinite);
        // 首个差异扫描只覆盖各层 uncovered valid，covered 数据留到重构/平均阶段。
        CompareOsiReferenceStage(lev, stage, reference, 0, false, true);
    }
}

void AmrCoreLBM::CheckOsiReferenceAllValid(
    int step, const char* stage) {
    if (!osiReferenceEnabled()) {
        return;
    }
    amrex::Print() << "osi_ab_all_valid_begin: step=" << step
                   << " stage=" << stage
                   << " finest_level=" << finest_level << '\n';
    for (int lev = 0; lev <= finest_level; ++lev) {
        const MultiFab& reference = f_old.at(lev);
        const bool nonfinite = reference.contains_nan(0, Q, 0) ||
                               reference.contains_inf(0, Q, 0);
        amrex::Print() << "osi_ab_all_valid_finite: step=" << step
                       << " stage=" << stage << " lev=" << lev
                       << " finite=" << (nonfinite ? 0 : 1) << '\n';
        AMREX_ALWAYS_ASSERT(!nonfinite);
        // 重构阶段必须包含 covered/interface valid cell，不能沿用 active-only 比较。
        CompareOsiReferenceStage(
            lev, stage, reference, 0, false, false);
    }
}

void AmrCoreLBM::CompareOsiReferenceStage(
    int lev, const char* stage, const amrex::MultiFab& reference,
    int compare_ngrow, bool exclude_physical_boundary, bool skip_covered) {
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        stream_mode == 1,
        "CompareOsiReferenceStage requires lbm.stream_mode=1");
    if (!osiReferenceEnabled()) {
        return;
    }

    amrex::MultiFab difference(
        reference.boxArray(), reference.DistributionMap(), Q,
        compare_ngrow);
    difference.setVal(0.0);
    Box comparison_domain = Geom(lev).growPeriodicDomain(compare_ngrow);
    if (exclude_physical_boundary) {
        for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
            if (!Geom(lev).isPeriodic(dir)) {
                comparison_domain.grow(dir, -1);
            }
        }
    }
    const amrex::MultiFab& state = osi_state[lev];
    const std::uint64_t phase = osi_phase[lev];

    for (MFIter mfi(reference, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        // 非周期域外 ghost 不属于 Communication 的有效同步语义。
        const Box valid_box = amrex::grow(mfi.validbox(), compare_ngrow) & comparison_domain;
        const Box ring_box =
            amrex::grow(mfi.validbox(), state.nGrowVect());
        const auto [fab, phase_shift] = OSI::MakeOsiFabContext(ring_box, phase);
        const Array4<const Real>& ab = reference.const_array(mfi);
        const Array4<const Real>& osi = state.const_array(mfi);
        const Array4<Real>& diff = difference.array(mfi);
        const bool has_fine = lev < finest_level;
        const auto covered = has_fine ? covered_mask.at(lev).const_array(mfi)
                                      : Array4<const int>{};

        amrex::ParallelFor(
            valid_box, Q,
            [=] AMREX_GPU_DEVICE(int i, int j, int k, int q) {
                if (has_fine && skip_covered && covered(i, j, k) != 0) {
                    return;
                }
                const auto raw = OSI::osi_address(
                    {i, j, k}, {e[q][0], e[q][1], e[q][2]}, fab,
                    phase_shift);
                diff(i, j, k, q) =
                    ab(i, j, k, q) - osi(raw.x, raw.y, raw.z, q);
            });
    }

    const Real raw_linf = difference.norm0(0, Q, IntVect(0));
    const Real linf = std::isfinite(raw_linf)
                          ? amrex::max(Real(0.0), raw_linf)
                          : raw_linf;
    constexpr Real tolerance = 1.0e-12;
    const bool comparison_failed =
        !std::isfinite(linf) || linf > tolerance;
    if (comparison_failed) {
        // e 位于设备侧，主机诊断必须使用独立的同序速度表。
        constexpr int ex[Q] = {
            0, 0, 0, -1, 1, 0, 0, -1, 1, -1, 1, 0, 0, -1,
            1, 0, 0, -1, 1, 1, -1, 1, -1, 1, -1, 1, -1};
        constexpr int ey[Q] = {
            0, 1, -1, 0, 0, 0, 0, 1, 1, -1, -1, 1, -1, 0,
            0, 1, -1, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1};
        constexpr int ez[Q] = {
            0, 0, 0, 0, 0, 1, -1, 0, 0, 0, 0, 1, 1, 1,
            1, -1, -1, -1, -1, 1, 1, 1, 1, -1, -1, -1, -1};
        bool found_nonfinite = false;
        bool found_finite_mismatch = false;
        Real local_max_error = 0.0;
        IntVect local_max_iv(0);
        IntVect local_max_raw(0);
        int local_max_q = -1;
        Real local_max_ab = 0.0;
        Real local_max_osi = 0.0;

        for (MFIter mfi(reference, false); mfi.isValid(); ++mfi) {
            const Box valid_box = mfi.validbox() & comparison_domain;
            const Box ring_box = state[mfi].box();
            FArrayBox host_reference(
                reference[mfi].box(), Q, The_Pinned_Arena());
            FArrayBox host_state(
                ring_box, Q, The_Pinned_Arena());
            Gpu::dtoh_memcpy(
                host_reference.dataPtr(), reference[mfi].dataPtr(),
                host_reference.nBytes());
            Gpu::dtoh_memcpy(
                host_state.dataPtr(), state[mfi].dataPtr(),
                host_state.nBytes());

            const auto ab = host_reference.const_array();
            const auto osi = host_state.const_array();
            const bool has_fine = lev < finest_level;
            std::unique_ptr<IArrayBox> host_covered;
            if (has_fine) {
                host_covered = std::make_unique<IArrayBox>(
                    covered_mask.at(lev)[mfi].box(), 1, The_Pinned_Arena());
                Gpu::dtoh_memcpy(host_covered->dataPtr(),
                                 covered_mask.at(lev)[mfi].dataPtr(),
                                 host_covered->nBytes());
            }
            const auto [fab, phase_shift] = OSI::MakeOsiFabContext(ring_box, phase);
            const IntVect lo = valid_box.smallEnd();
            const IntVect hi = valid_box.bigEnd();

            for (int q = 0; q < Q; ++q) {
                for (int k = lo[2]; k <= hi[2]; ++k) {
                    for (int j = lo[1]; j <= hi[1]; ++j) {
                        for (int i = lo[0]; i <= hi[0]; ++i) {
                            if (has_fine && skip_covered &&
                                host_covered->const_array()(i, j, k) != 0) {
                                continue;
                            }
                            const auto raw = OSI::osi_address(
                                {i, j, k},
                                {ex[q], ey[q], ez[q]}, fab,
                                phase_shift);
                            const Real ab_value = ab(i, j, k, q);
                            const Real osi_value =
                                osi(raw.x, raw.y, raw.z, q);
                            const Real error =
                                std::abs(ab_value - osi_value);
                            const bool nonfinite =
                                !std::isfinite(ab_value) ||
                                !std::isfinite(osi_value) ||
                                !std::isfinite(error);
                            if (nonfinite && !found_nonfinite) {
                                found_nonfinite = true;
                                local_max_iv =
                                    IntVect(AMREX_D_DECL(i, j, k));
                                local_max_raw = IntVect(
                                    AMREX_D_DECL(raw.x, raw.y, raw.z));
                                local_max_q = q;
                                local_max_ab = ab_value;
                                local_max_osi = osi_value;
                            } else if (!found_nonfinite &&
                                       error > tolerance &&
                                       (!found_finite_mismatch ||
                                        error > local_max_error)) {
                                found_finite_mismatch = true;
                                local_max_error = error;
                                local_max_iv =
                                    IntVect(AMREX_D_DECL(i, j, k));
                                local_max_raw = IntVect(
                                    AMREX_D_DECL(raw.x, raw.y, raw.z));
                                local_max_q = q;
                                local_max_ab = ab_value;
                                local_max_osi = osi_value;
                            }
                        }
                    }
                }
            }
        }

        auto output = amrex::AllPrint();
        output.SetPrecision(17);
        output << "osi_ab_stage_failure: stage=" << stage
               << " lev=" << lev
               << " phase=" << phase
               << " global_linf=" << linf
               << " local_failure="
               << ((found_nonfinite || found_finite_mismatch) ? 1 : 0);
        if (found_nonfinite || found_finite_mismatch) {
            output << " failure_kind="
                   << (found_nonfinite ? "nonfinite" : "finite_mismatch")
                   << " local_max_error=" << local_max_error
                   << " logical=" << local_max_iv
                   << " q=" << local_max_q
                   << " velocity=(" << ex[local_max_q] << ','
                   << ey[local_max_q] << ',' << ez[local_max_q] << ')'
                   << " raw=" << local_max_raw
                   << " source_logical="
                   << (local_max_iv - IntVect(AMREX_D_DECL(
                                          ex[local_max_q], ey[local_max_q], ez[local_max_q])))
                   << " physical_boundary="
                   << ((!Geom(lev).isPeriodic(0) &&
                        (local_max_iv[0] == Geom(lev).Domain().smallEnd(0) ||
                         local_max_iv[0] == Geom(lev).Domain().bigEnd(0))) ||
                               (!Geom(lev).isPeriodic(1) &&
                                (local_max_iv[1] == Geom(lev).Domain().smallEnd(1) ||
                                 local_max_iv[1] == Geom(lev).Domain().bigEnd(1))) ||
                               (!Geom(lev).isPeriodic(2) &&
                                (local_max_iv[2] == Geom(lev).Domain().smallEnd(2) ||
                                 local_max_iv[2] == Geom(lev).Domain().bigEnd(2)))
                           ? 1
                           : 0)
                   << " ab=" << local_max_ab
                   << " osi=" << local_max_osi;
        }
        output << '\n';
    }
    bool continue_on_mismatch = false;
    ParmParse("verification").query("osi_ab_continue_on_mismatch", continue_on_mismatch);
    if (comparison_failed && continue_on_mismatch) {
        // 仅供独立 OSI 状态对照：保留失败记录，继续写出诊断态 checkpoint。
        return;
    }
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        !comparison_failed,
        "OSI differs from the A-B reference; see osi_ab_stage_failure above");
    amrex::Print() << "osi_ab_stage: stage=" << stage
                   << " lev=" << lev
                   << " phase=" << osi_phase[lev]
                   << " linf=" << linf << '\n';
}

void AmrCoreLBM::CompareOsiStreamSources(int lev, const char* stage) {
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        stream_mode == 1,
        "CompareOsiStreamSources requires lbm.stream_mode=1");
    if (!osiReferenceEnabled()) {
        return;
    }

    const MultiFab& reference = f_old.at(lev);
    const MultiFab& state = osi_state.at(lev);
    const bool has_fine = lev < finest_level;
    Long target_count = 0;
    Long source_count = 0;
    Long source_valid_count = 0;
    Long source_ghost_count = 0;
    Long source_physical_ghost_count = 0;
    Long source_internal_ghost_count = 0;
    Long source_interface_count = 0;
    Long source_covered_count = 0;
    Long mismatch_count = 0;
    Long mismatch_valid = 0;
    Long mismatch_ghost = 0;
    Long mismatch_physical_ghost = 0;
    Long mismatch_internal_ghost = 0;
    Long mismatch_interface = 0;
    Long mismatch_covered = 0;
    Real max_error = 0.0;
    IntVect max_target(0);
    IntVect max_source(0);
    int max_q = -1;
    constexpr int ex[Q] = {
        0, 0, 0, -1, 1, 0, 0, -1, 1, -1, 1, 0, 0, -1,
        1, 0, 0, -1, 1, 1, -1, 1, -1, 1, -1, 1, -1};
    constexpr int ey[Q] = {
        0, 1, -1, 0, 0, 0, 0, 1, 1, -1, -1, 1, -1, 0,
        0, 1, -1, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1};
    constexpr int ez[Q] = {
        0, 0, 0, 0, 0, 1, -1, 0, 0, 0, 0, 1, 1, 1,
        1, -1, -1, -1, -1, 1, 1, 1, 1, -1, -1, -1, -1};

    for (MFIter mfi(reference, false); mfi.isValid(); ++mfi) {
        const Box valid = mfi.validbox();
        const Box ring = state[mfi].box();
        FArrayBox host_reference(reference[mfi].box(), Q, The_Pinned_Arena());
        FArrayBox host_state(state[mfi].box(), Q, The_Pinned_Arena());
        Gpu::dtoh_memcpy(host_reference.dataPtr(), reference[mfi].dataPtr(),
                         host_reference.nBytes());
        Gpu::dtoh_memcpy(host_state.dataPtr(), state[mfi].dataPtr(),
                         host_state.nBytes());
        const auto ab = host_reference.const_array();
        const auto osi = host_state.const_array();
        const auto [fab, phase_shift] = OSI::MakeOsiFabContext(ring, osi_phase.at(lev));

        std::unique_ptr<IArrayBox> host_covered;
        std::unique_ptr<IArrayBox> host_interface;
        if (has_fine) {
            host_covered = std::make_unique<IArrayBox>(
                covered_mask.at(lev)[mfi].box(), 1, The_Pinned_Arena());
            host_interface = std::make_unique<IArrayBox>(
                interface_mask.at(lev)[mfi].box(), 1, The_Pinned_Arena());
            Gpu::dtoh_memcpy(host_covered->dataPtr(),
                             covered_mask.at(lev)[mfi].dataPtr(),
                             host_covered->nBytes());
            Gpu::dtoh_memcpy(host_interface->dataPtr(),
                             interface_mask.at(lev)[mfi].dataPtr(),
                             host_interface->nBytes());
        }

        for (int k = valid.smallEnd(2); k <= valid.bigEnd(2); ++k) {
            for (int j = valid.smallEnd(1); j <= valid.bigEnd(1); ++j) {
                for (int i = valid.smallEnd(0); i <= valid.bigEnd(0); ++i) {
                    if (has_fine &&
                        host_covered->const_array()(i, j, k) != 0) {
                        continue;
                    }
                    ++target_count;
                    for (int q = 0; q < Q; ++q) {
                        const IntVect target(AMREX_D_DECL(i, j, k));
                        const IntVect source(AMREX_D_DECL(
                            i - ex[q], j - ey[q], k - ez[q]));
                        if (!ring.contains(source)) {
                            continue;
                        }
                        ++source_count;
                        const bool source_valid = valid.contains(source);
                        const bool source_ghost = !source_valid;
                        const bool source_physical_ghost =
                            source_ghost && !Geom(lev).Domain().contains(source);
                        const bool source_internal_ghost =
                            source_ghost && !source_physical_ghost;
                        const bool source_covered =
                            has_fine &&
                            host_covered->const_array()(source[0], source[1],
                                                        source[2]) != 0;
                        const bool source_interface =
                            has_fine &&
                            host_interface->const_array()(source[0], source[1],
                                                          source[2]) != 0;
                        source_valid_count += source_valid;
                        source_ghost_count += source_ghost;
                        source_physical_ghost_count += source_physical_ghost;
                        source_internal_ghost_count += source_internal_ghost;
                        source_covered_count += source_covered;
                        source_interface_count += source_interface;
                        const auto raw = OSI::osi_address(
                            {source[0], source[1], source[2]},
                            {ex[q], ey[q], ez[q]}, fab, phase_shift);
                        const Real error =
                            std::abs(ab(source[0], source[1], source[2], q) -
                                     osi(raw.x, raw.y, raw.z, q));
                        if (error > Real(1.e-12)) {
                            ++mismatch_count;
                            mismatch_valid += source_valid;
                            mismatch_ghost += source_ghost;
                            mismatch_physical_ghost += source_physical_ghost;
                            mismatch_internal_ghost += source_internal_ghost;
                            mismatch_covered += source_covered;
                            mismatch_interface += source_interface;
                            if (error > max_error) {
                                max_error = error;
                                max_target = target;
                                max_source = source;
                                max_q = q;
                            }
                        }
                    }
                }
            }
        }
    }

    ParallelDescriptor::ReduceLongSum(target_count);
    ParallelDescriptor::ReduceLongSum(source_count);
    ParallelDescriptor::ReduceLongSum(source_valid_count);
    ParallelDescriptor::ReduceLongSum(source_ghost_count);
    ParallelDescriptor::ReduceLongSum(source_physical_ghost_count);
    ParallelDescriptor::ReduceLongSum(source_internal_ghost_count);
    ParallelDescriptor::ReduceLongSum(source_interface_count);
    ParallelDescriptor::ReduceLongSum(source_covered_count);
    ParallelDescriptor::ReduceLongSum(mismatch_count);
    ParallelDescriptor::ReduceLongSum(mismatch_valid);
    ParallelDescriptor::ReduceLongSum(mismatch_ghost);
    ParallelDescriptor::ReduceLongSum(mismatch_physical_ghost);
    ParallelDescriptor::ReduceLongSum(mismatch_internal_ghost);
    ParallelDescriptor::ReduceLongSum(mismatch_interface);
    ParallelDescriptor::ReduceLongSum(mismatch_covered);
    ParallelDescriptor::ReduceRealMax(max_error);
    amrex::Print() << "osi_ab_stream_sources: stage=" << stage
                   << " lev=" << lev
                   << " phase=" << osi_phase.at(lev)
                   << " targets=" << target_count
                   << " sources=" << source_count
                   << " source_valid=" << source_valid_count
                   << " source_ghost=" << source_ghost_count
                   << " source_physical_ghost=" << source_physical_ghost_count
                   << " source_internal_ghost=" << source_internal_ghost_count
                   << " source_interface=" << source_interface_count
                   << " source_covered=" << source_covered_count
                   << " mismatches=" << mismatch_count
                   << " mismatch_valid=" << mismatch_valid
                   << " mismatch_ghost=" << mismatch_ghost
                   << " mismatch_physical_ghost=" << mismatch_physical_ghost
                   << " mismatch_internal_ghost=" << mismatch_internal_ghost
                   << " mismatch_interface=" << mismatch_interface
                   << " mismatch_covered=" << mismatch_covered
                   << " linf=" << max_error;
    if (max_q >= 0) {
        amrex::Print() << " max_target=" << max_target
                       << " max_source=" << max_source
                       << " q=" << max_q;
    }
    amrex::Print() << '\n';
}

void AmrCoreLBM::AdvanceAndCheckOsiReference(int lev, int step) {
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        run_mode == RunMode::OsiLockstep,
        "AdvanceAndCheckOsiReference requires the OSI oracle");
    ComputeMacroLevel(lev);
    bool continue_on_mismatch = false;
    ParmParse("verification").query("osi_ab_continue_on_mismatch", continue_on_mismatch);
    amrex::Print() << "osi_ab: step=" << step
                   << " phase=" << osi_phase[lev]
                   << " staged_check="
                   << (continue_on_mismatch ? "completed_with_failures_allowed"
                                            : "passed")
                   << '\n';
}

void AmrCoreLBM::PrintDdfChecksums(int step) {
    for (int lev = 0; lev <= finest_level; ++lev) {
        const bool has_fine = lev < finest_level;
        Real valid_checksum = 0.0;
        Real active_checksum = 0.0;
        GpuArray<Real, Q> active_q{};

        if (stream_mode == 1) {
            const MultiFab& state = osi_state.at(lev);
            MultiFab canonical(state.boxArray(), state.DistributionMap(),
                               Q, state.nGrowVect());
            DecodeOsiValid(state, osi_phase.at(lev), canonical);
            for (int q = 0; q < Q; ++q) {
                const Real valid_q = canonical.sum(q, 0);
                valid_checksum += valid_q;
                if (!has_fine) {
                    active_q[q] = valid_q;
                    active_checksum += valid_q;
                    continue;
                }
                for (MFIter mfi(canonical, false); mfi.isValid(); ++mfi) {
                    const Box bx = mfi.validbox();
                    const auto values = canonical.array(mfi);
                    const auto covered = covered_mask.at(lev).const_array(mfi);
                    amrex::ParallelFor(
                        bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                            if (covered(i, j, k) != 0) {
                                values(i, j, k, q) = Real(0.0);
                            }
                        });
                }
                active_q[q] = canonical.sum(q, 0);
                active_checksum += active_q[q];
            }
        } else {
            const MultiFab& state = f_old.at(lev);
            MultiFab active_component(
                state.boxArray(), state.DistributionMap(), 1, 0);
            for (int q = 0; q < Q; ++q) {
                for (MFIter mfi(state, false); mfi.isValid(); ++mfi) {
                    const Box bx = mfi.validbox();
                    const auto src = state.const_array(mfi);
                    const auto dst = active_component.array(mfi);
                    if (!has_fine) {
                        amrex::ParallelFor(
                            bx,
                            [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                                dst(i, j, k) = src(i, j, k, q);
                            });
                        continue;
                    }
                    const auto covered = covered_mask.at(lev).const_array(mfi);
                    amrex::ParallelFor(
                        bx,
                        [=] AMREX_GPU_DEVICE(int i, int j, int k) {
                            const bool active = covered(i, j, k) == 0;
                            dst(i, j, k) =
                                active ? src(i, j, k, q) : Real(0.0);
                        });
                }
                valid_checksum += state.sum(q, 0);
                active_q[q] = active_component.sum(0, 0);
                active_checksum += active_q[q];
            }
        }
        if (ParallelDescriptor::IOProcessor()) {
            amrex::Print() << "ddf_checksum: step=" << step
                           << " lev=" << lev
                           << " valid_sum=" << valid_checksum
                           << " active_sum=" << active_checksum;
            for (int q = 0; q < Q; ++q) {
                amrex::Print() << " active_q" << q << "=" << active_q[q];
            }
            amrex::Print() << '\n';
        }
    }
}

void AmrCoreLBM::PrintLevelDdfChecksum(const char* stage, int lev, bool use_new) {
    GpuArray<Real, Q> component_sum{};
    GpuArray<Real, Q> active_component_sum{};
    Real valid_sum = 0.0;
    Real active_sum = 0.0;

    if (stream_mode == 1) {
        const MultiFab& state = osi_state.at(lev);
        MultiFab canonical(state.boxArray(), state.DistributionMap(),
                           Q, state.nGrowVect());
        DecodeOsiValid(state, osi_phase.at(lev), canonical);
        for (int q = 0; q < Q; ++q) {
            component_sum[q] = canonical.sum(q, 0);
            valid_sum += component_sum[q];
            active_component_sum[q] = component_sum[q];
            active_sum += active_component_sum[q];
        }
    } else {
        const MultiFab& state = use_new ? f_new.at(lev) : f_old.at(lev);
        for (int q = 0; q < Q; ++q) {
            component_sum[q] = state.sum(q, 0);
            valid_sum += component_sum[q];

            if (lev < finest_level) {
                MultiFab active(state.boxArray(), state.DistributionMap(), 1, 0,
                                MFInfo().SetArena(The_Arena()));
                for (MFIter mfi(state, false); mfi.isValid(); ++mfi) {
                    const Box bx = mfi.validbox();
                    const auto src = state.const_array(mfi);
                    const auto dst = active.array(mfi);
                    const auto covered = covered_mask.at(lev).const_array(mfi);
                    ParallelFor(bx, [=] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {
                        dst(i, j, k) = (covered(i, j, k) == 0)
                                           ? src(i, j, k, q)
                                           : Real(0.0);
                    });
                }
                active_component_sum[q] = active.sum(0, 0);
            } else {
                active_component_sum[q] = component_sum[q];
            }
            active_sum += active_component_sum[q];
        }
    }

    if (ParallelDescriptor::IOProcessor()) {
        amrex::Print() << "NEW_LEVEL_DIAG stage=" << stage
                       << " mode=" << stream_mode
                       << " lev=" << lev
                       << " array=" << (use_new ? "f_new" : "f_old")
                       << " valid_sum=" << valid_sum
                       << " active_sum=" << active_sum;
        for (int q = 0; q < Q; ++q) {
            amrex::Print() << " q" << q << "=" << component_sum[q]
                           << " active_q" << q << "=" << active_component_sum[q];
        }
        amrex::Print() << '\n';
    }
}

void AmrCoreLBM::PrintInterpolationDiagnostics(const char* stage, int lev) {
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        stream_mode == 0,
        "PrintInterpolationDiagnostics is only available for the A-B path");
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        lev > 0 && lev <= finest_level,
        "PrintInterpolationDiagnostics requires an active fine level");

    const auto& fine_boxes = interp_direct_fine_boxes.at(lev);
    if (fine_boxes.empty()) {
        amrex::Print() << "INTERP_DIAG stage=" << stage
                       << " lev=" << lev << " entries=0\n";
        return;
    }

    const MultiFab& coarse_stage = interp_direct_coarse_stage.at(lev);
    const MultiFab& fine_state = f_old.at(lev);
    const auto& coarse_boxes = coarse_stage.boxArray();
    long long fine_points = 0;
    long long coarse_points = 0;
    for (const Box& box : fine_boxes) {
        fine_points += box.numPts();
    }
    for (int ibox = 0; ibox < coarse_boxes.size(); ++ibox) {
        coarse_points += coarse_boxes[ibox].numPts();
    }

    amrex::Print() << "INTERP_DIAG stage=" << stage
                   << " lev=" << lev
                   << " entries=" << fine_boxes.size()
                   << " fine_points=" << fine_points
                   << " coarse_points=" << coarse_points;
    if (!fine_boxes.empty()) {
        amrex::Print() << " first_fine=" << fine_boxes.front()
                       << " first_coarse=" << coarse_boxes[0]
                       << " last_fine=" << fine_boxes.back()
                       << " last_coarse="
                       << coarse_boxes[coarse_boxes.size() - 1];
    }
    for (int q : {0, 3, 18, 26}) {
        amrex::Print() << " coarse_q" << q << '=' << coarse_stage.norm1(q, 0)
                       << " fine_q" << q << "_valid="
                       << fine_state.norm1(q, 0)
                       << " fine_q" << q << "_grow="
                       << fine_state.norm1(q, fine_state.nGrowVect().max());
    }
    amrex::Print() << '\n';
}

void AmrCoreLBM::PrintRegridDiagnostics() const {
    if (!ParallelDescriptor::IOProcessor()) {
        return;
    }

    amrex::Print() << "REGRID_DIAG finest_level=" << finest_level << '\n';
    for (int lev = 0; lev <= finest_level; ++lev) {
        const auto& ba = boxArray(lev);
        amrex::Print() << "REGRID_DIAG lev=" << lev
                       << " boxes=" << ba.size()
                       << " valid_cells=" << ba.numPts();
        if (lev < max_level && lev < regrid_tag_counts.size() &&
            regrid_tag_counts[lev] >= 0) {
            amrex::Print() << " tagged_to_lev=" << (lev + 1)
                           << " tag_cells=" << regrid_tag_counts[lev];
        }
        amrex::Print() << '\n';
    }

    for (int lev = 0; lev < finest_level; ++lev) {
        long fine_cells_per_coarse = 1;
        const auto ratio = refRatio(lev);
        for (int dim = 0; dim < AMREX_SPACEDIM; ++dim) {
            fine_cells_per_coarse *= ratio[dim];
        }
        const auto expected_covered =
            boxArray(lev + 1).numPts() / fine_cells_per_coarse;
        const auto uncovered =
            boxArray(lev).numPts() - covered_cell_counts[lev];
        amrex::Print() << "CF_MASK_DIAG lev=" << lev
                       << " covered=" << covered_cell_counts[lev]
                       << " covered_expected=" << expected_covered
                       << " interface=" << interface_cell_counts[lev]
                       << " uncovered=" << uncovered << '\n';
    }
}

void AmrCoreLBM::PrintParticleChecksums(int step) const {
    if (!particle_checksum) {
        return;
    }

    for (int iparticle = 0; iparticle < particle_num; ++iparticle) {
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            particles[iparticle] != nullptr,
            "verification.particle_checksum requires initialized particles");
        const auto& container = *particles[iparticle];
        using ConstPTDType = LagrangeParticleContainer::ConstPTDType;

        const Long count = container.TotalNumberOfParticles();
        GpuArray<Real, AMREX_SPACEDIM> position_sum{};
        GpuArray<Real, PIdx::nattribs> attribute_sum{};
        for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
            Real value = amrex::ReduceSum(
                container,
                [=] AMREX_GPU_HOST_DEVICE(
                    const ConstPTDType& ptd, int i) noexcept -> Real {
                    return ptd.pos(dir, i);
                });
            ParallelDescriptor::ReduceRealSum(value);
            position_sum[dir] = value;
        }
        for (int n = 0; n < PIdx::nattribs; ++n) {
            Real value = amrex::ReduceSum(
                container,
                [=] AMREX_GPU_HOST_DEVICE(
                    const ConstPTDType& ptd, int i) noexcept -> Real {
                    return ptd.rdata(n)[i];
                });
            ParallelDescriptor::ReduceRealSum(value);
            attribute_sum[n] = value;
        }

        amrex::Print() << std::setprecision(17)
                       << "particle_checksum: step=" << step
                       << " particle=" << iparticle
                       << " count=" << count;
        for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
            amrex::Print() << " pos" << dir << "=" << position_sum[dir];
        }
        for (int n = 0; n < PIdx::nattribs; ++n) {
            amrex::Print() << " attr" << n << "=" << attribute_sum[n];
        }
        amrex::Print() << '\n';
    }
}

void AmrCoreLBM::DiagnoseBoundaryResult(int lev) {
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        stream_mode == 0,
        "DiagnoseBoundaryResult is only available for the A-B path");
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        lev >= 0 && lev <= finest_level,
        "DiagnoseBoundaryResult requires an active AMR level");
    Gpu::streamSynchronize();
    const Box domain = Geom(lev).Domain();
    const auto is_periodic = Geom(lev).isPeriodicArray();
    const bool has_fine = lev < finest_level;
    const MultiFab& f_new_lev = f_new.at(lev);
    GpuArray<Long, Q> bad{};
    GpuArray<Long, Q> bad_covered{};
    GpuArray<Long, Q> bad_interface{};
    GpuArray<Long, Q> bad_physical{};
    GpuArray<Long, Q> bad_fab_edge{};
    GpuArray<Long, Q> bad_neighbor_nonfinite{};
    GpuArray<Long, Q> bad_active{};
    GpuArray<Long, Q> bad_active_physical{};
    std::array<IntVect, Q> min_iv;
    std::array<IntVect, Q> max_iv;
    std::array<std::vector<IntVect>, Q> samples;
    std::array<std::vector<IntVect>, Q> sample_neighbors;
    std::array<std::vector<IntVect>, Q> active_samples;
    std::array<std::vector<IntVect>, Q> active_physical_samples;

    for (MFIter mfi(f_new_lev, false); mfi.isValid(); ++mfi) {
        const Box bx = mfi.validbox();
        FArrayBox host_state(f_new_lev[mfi].box(), Q,
                             The_Pinned_Arena());
        Gpu::dtoh_memcpy(host_state.dataPtr(),
                         f_new_lev[mfi].dataPtr(),
                         host_state.nBytes());
        IArrayBox host_covered(
            has_fine ? covered_mask[lev][mfi].box() : bx, 1,
            The_Pinned_Arena());
        IArrayBox host_interface(
            has_fine ? interface_mask[lev][mfi].box() : bx, 1,
            The_Pinned_Arena());
        if (has_fine) {
            Gpu::dtoh_memcpy(host_covered.dataPtr(),
                             covered_mask[lev][mfi].dataPtr(),
                             host_covered.nBytes());
            Gpu::dtoh_memcpy(host_interface.dataPtr(),
                             interface_mask[lev][mfi].dataPtr(),
                             host_interface.nBytes());
        }
        const auto state = host_state.const_array();
        const auto covered = host_covered.const_array();
        const auto interface = host_interface.const_array();
        const IntVect lo = bx.smallEnd();
        const IntVect hi_box = bx.bigEnd();
        for (int k = lo[2]; k <= hi_box[2]; ++k) {
            for (int j = lo[1]; j <= hi_box[1]; ++j) {
                for (int i = lo[0]; i <= hi_box[0]; ++i) {
                    const IntVect iv(AMREX_D_DECL(i, j, k));
                    int ni = i;
                    int nj = j;
                    int nk = k;
                    if (!is_periodic[0] && i == domain.smallEnd(0))
                        ni = i + 1;
                    if (!is_periodic[0] && i == domain.bigEnd(0))
                        ni = i - 1;
                    if (!is_periodic[1] && j == domain.smallEnd(1))
                        nj = j + 1;
                    if (!is_periodic[1] && j == domain.bigEnd(1))
                        nj = j - 1;
                    if (!is_periodic[2] && k == domain.smallEnd(2))
                        nk = k + 1;
                    if (!is_periodic[2] && k == domain.bigEnd(2))
                        nk = k - 1;
                    const IntVect neighbor(AMREX_D_DECL(ni, nj, nk));
                    const bool physical = neighbor != iv;
                    const bool fab_edge = i == lo[0] || i == hi_box[0] ||
                                          j == lo[1] || j == hi_box[1] ||
                                          k == lo[2] || k == hi_box[2];
                    bool neighbor_nonfinite = false;
                    if (host_state.box().contains(neighbor)) {
                        for (int nq = 0; nq < Q; ++nq) {
                            neighbor_nonfinite = neighbor_nonfinite ||
                                                 !std::isfinite(state(neighbor, nq));
                        }
                    }
                    for (int q = 0; q < Q; ++q) {
                        if (std::isfinite(state(i, j, k, q)))
                            continue;
                        if (bad[q] == 0) {
                            min_iv[q] = iv;
                            max_iv[q] = iv;
                        } else {
                            min_iv[q].min(iv);
                            max_iv[q].max(iv);
                        }
                        ++bad[q];
                        bad_covered[q] += has_fine && covered(i, j, k) != 0;
                        bad_interface[q] += has_fine && interface(i, j, k) != 0;
                        bad_physical[q] += physical;
                        bad_fab_edge[q] += fab_edge;
                        bad_neighbor_nonfinite[q] += physical && neighbor_nonfinite;
                        const bool active = !has_fine || covered(i, j, k) == 0;
                        bad_active[q] += active;
                        bad_active_physical[q] += active && physical;
                        if (active && active_samples[q].size() < 12) {
                            active_samples[q].push_back(iv);
                        }
                        if (active && physical &&
                            active_physical_samples[q].size() < 12) {
                            active_physical_samples[q].push_back(iv);
                        }
                        if (samples[q].size() < 8) {
                            samples[q].push_back(iv);
                            sample_neighbors[q].push_back(neighbor);
                        }
                    }
                }
            }
        }
    }
    for (int q = 0; q < Q; ++q) {
        if (bad[q] == 0)
            continue;
        amrex::Print() << "BOUNDARY_NONFINITE lev=" << lev << " q=" << q
                       << " count=" << bad[q]
                       << " covered=" << bad_covered[q]
                       << " interface=" << bad_interface[q]
                       << " physical_boundary=" << bad_physical[q]
                       << " fab_edge=" << bad_fab_edge[q]
                       << " active=" << bad_active[q]
                       << " active_physical_boundary="
                       << bad_active_physical[q]
                       << " boundary_neighbor_nonfinite="
                       << bad_neighbor_nonfinite[q]
                       << " bbox=" << Box(min_iv[q], max_iv[q]);
        for (std::size_t n = 0; n < samples[q].size(); ++n) {
            amrex::Print() << " sample" << n << "=" << samples[q][n]
                           << " neighbor" << n << "="
                           << sample_neighbors[q][n];
        }
        for (std::size_t n = 0; n < active_samples[q].size(); ++n) {
            amrex::Print() << " active_sample" << n << "="
                           << active_samples[q][n];
        }
        for (std::size_t n = 0; n < active_physical_samples[q].size(); ++n) {
            amrex::Print() << " active_physical_sample" << n << "="
                           << active_physical_samples[q][n];
        }
        amrex::Print() << '\n';
    }
}

void AmrCoreLBM::DiagnoseStreamResult(int lev, int n) {
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        stream_mode == 0,
        "DiagnoseStreamResult is only available for the A-B path");
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        lev >= 0 && lev <= finest_level,
        "DiagnoseStreamResult requires an active AMR level");
    AMREX_ALWAYS_ASSERT(n >= 1);
    AMREX_ALWAYS_ASSERT(n - 1 <= 1);

    const MultiFab& f_old_lev = f_old.at(lev);
    const MultiFab& f_new_lev = f_new.at(lev);
    const bool has_fine_level = lev < finest_level;
    Long launch_cells = 0;
    for (MFIter mfi(f_old_lev, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
        launch_cells += mfi.growntilebox(n - 1).numPts();
    }

    amrex::Print() << "STREAM_INPUT_NORM lev=" << lev;
    for (int q : {0, 3, 18, 26}) {
        amrex::Print() << " q" << q << "_valid=" << f_old_lev.norm1(q, 0)
                       << " q" << q << "_grow="
                       << f_old_lev.norm1(q, n - 1);
    }
    amrex::Print() << '\n';

    Gpu::streamSynchronize();
    const Box domain = Geom(lev).Domain();
    GpuArray<Long, Q> bad{};
    GpuArray<Long, Q> bad_covered{};
    GpuArray<Long, Q> bad_interface{};
    GpuArray<Long, Q> bad_physical{};
    GpuArray<Long, Q> bad_fab_edge{};
    GpuArray<Long, Q> bad_source{};
    GpuArray<Long, Q> source_ghost{};
    GpuArray<Long, Q> source_outside_fab{};
    std::array<IntVect, Q> first_iv{};
    std::array<IntVect, Q> first_src{};
    std::array<int, Q> first_has{};
    constexpr int ex[Q] = {0, 0, 0, -1, 1, 0, 0, -1, 1, -1, 1, 0, 0, -1, 1, 0, 0, -1, 1, 1, -1, 1, -1, 1, -1, 1, -1};
    constexpr int ey[Q] = {0, 1, -1, 0, 0, 0, 0, 1, 1, -1, -1, 1, -1, 0, 0, 1, -1, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1};
    constexpr int ez[Q] = {0, 0, 0, 0, 0, 1, -1, 0, 0, 0, 0, 1, 1, 1, 1, -1, -1, -1, -1, 1, 1, 1, 1, -1, -1, -1, -1};
    for (MFIter mfi(f_new_lev, false); mfi.isValid(); ++mfi) {
        const Box bx = mfi.validbox();
        FArrayBox host_new(f_new_lev[mfi].box(), Q, The_Pinned_Arena());
        Gpu::dtoh_memcpy(host_new.dataPtr(), f_new_lev[mfi].dataPtr(),
                         host_new.nBytes());
        const Box source_box = f_old_lev[mfi].box();
        FArrayBox host_old(source_box, Q, The_Pinned_Arena());
        Gpu::dtoh_memcpy(host_old.dataPtr(), f_old_lev[mfi].dataPtr(),
                         host_old.nBytes());
        const bool has_masks = has_fine_level;
        IArrayBox host_covered(
            has_masks ? covered_mask[lev][mfi].box() : bx, 1,
            The_Pinned_Arena());
        IArrayBox host_interface(
            has_masks ? interface_mask[lev][mfi].box() : bx, 1,
            The_Pinned_Arena());
        if (has_masks) {
            Gpu::dtoh_memcpy(host_covered.dataPtr(),
                             covered_mask[lev][mfi].dataPtr(),
                             host_covered.nBytes());
            Gpu::dtoh_memcpy(host_interface.dataPtr(),
                             interface_mask[lev][mfi].dataPtr(),
                             host_interface.nBytes());
        }
        const auto values = host_new.const_array();
        const auto sources = host_old.const_array();
        const auto covered = host_covered.const_array();
        const auto interface = host_interface.const_array();
        const IntVect lo = bx.smallEnd();
        const IntVect hi = bx.bigEnd();
        for (int k = lo[2]; k <= hi[2]; ++k) {
            for (int j = lo[1]; j <= hi[1]; ++j) {
                for (int i = lo[0]; i <= hi[0]; ++i) {
                    const IntVect iv(AMREX_D_DECL(i, j, k));
                    const bool phys =
                        (!Geom(lev).isPeriodic(0) &&
                         (i == domain.smallEnd(0) || i == domain.bigEnd(0))) ||
                        (!Geom(lev).isPeriodic(1) &&
                         (j == domain.smallEnd(1) || j == domain.bigEnd(1))) ||
                        (!Geom(lev).isPeriodic(2) &&
                         (k == domain.smallEnd(2) || k == domain.bigEnd(2)));
                    const bool edge = i == lo[0] || i == hi[0] ||
                                      j == lo[1] || j == hi[1] ||
                                      k == lo[2] || k == hi[2];
                    for (int q = 0; q < Q; ++q) {
                        if (std::isfinite(values(i, j, k, q)))
                            continue;
                        ++bad[q];
                        const bool cov = has_fine_level &&
                                         covered(i, j, k) != 0;
                        const bool intf = has_fine_level &&
                                          interface(i, j, k) != 0;
                        bad_covered[q] += cov;
                        bad_interface[q] += intf;
                        bad_physical[q] += phys;
                        bad_fab_edge[q] += edge;
                        const IntVect src_iv = iv -
                                               IntVect(AMREX_D_DECL(ex[q], ey[q], ez[q]));
                        const bool src_in_valid = bx.contains(src_iv);
                        const bool src_in_fab = source_box.contains(src_iv);
                        source_ghost[q] += !src_in_valid;
                        source_outside_fab[q] += !src_in_fab;
                        if (src_in_fab && !std::isfinite(sources(src_iv, q))) {
                            ++bad_source[q];
                        }
                        if (!first_has[q]) {
                            first_has[q] = 1;
                            first_iv[q] = iv;
                            first_src[q] = iv - IntVect(AMREX_D_DECL(ex[q], ey[q], ez[q]));
                        }
                    }
                }
            }
        }
    }
    for (int q = 0; q < Q; ++q) {
        if (bad[q] != 0) {
            amrex::Print() << "STREAM_NONFINITE lev=" << lev << " q=" << q
                           << " count=" << bad[q]
                           << " covered=" << bad_covered[q]
                           << " interface=" << bad_interface[q]
                           << " physical_boundary=" << bad_physical[q]
                           << " fab_edge=" << bad_fab_edge[q]
                           << " source_nonfinite=" << bad_source[q]
                           << " source_ghost=" << source_ghost[q]
                           << " source_outside_fab=" << source_outside_fab[q]
                           << " first_iv=" << first_iv[q]
                           << " first_pull_src=" << first_src[q] << '\n';
        }
    }
    if (ParallelDescriptor::IOProcessor()) {
        Long covered_cells = (has_fine_level) ? covered_mask[lev].sum(0, 0) : 0;
        amrex::Print() << "STREAM_RANGE lev=" << lev << " n=" << n << " fabs=" << f_old_lev.boxArray().size()
                       << " launch_cells=" << launch_cells << " covered_cells=" << covered_cells
                       << " stream_cells=" << (launch_cells - covered_cells) << '\n';
        for (int ib = 0; ib < f_old_lev.boxArray().size(); ++ib) {
            amrex::Print() << "STREAM_FAB lev=" << lev << " fab=" << ib << " box=" << f_old_lev.boxArray()[ib] << '\n';
        }
    }
}

void AmrCoreLBM::CompareDdfCheckpoint(
    const std::string& checkpoint_path) const {
    const std::string header_file = checkpoint_path + "/Header";
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        amrex::FileExists(header_file),
        "DDF reference checkpoint is missing Header: " + header_file);

    amrex::Vector<char> header_chars;
    ParallelDescriptor::ReadAndBcastFile(header_file, header_chars);
    std::istringstream header{std::string(header_chars.dataPtr())};

    std::string label;
    std::getline(header, label);
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        label == checkpoint_label_v1 || label == checkpoint_label_v2,
        "Invalid DDF reference checkpoint: " + checkpoint_path);

    std::string line;
    if (label == checkpoint_label_v2) {
        std::getline(header, line); // canonical layout identifier
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            line == checkpoint_layout_ab || line == checkpoint_layout_osi,
            "Unknown DDF checkpoint layout: " + line);
    }
    std::getline(header, line); // step
    std::getline(header, line); // time
    int reference_finest = -1;
    header >> reference_finest;
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        reference_finest == finest_level,
        "DDF comparison requires identical finest levels");

    constexpr const char* level_prefix = "Level_";
    Real global_linf = 0.0;
    Real global_l1 = 0.0;
    Real global_diff_l2_sq = 0.0;
    Real global_ref_l2_sq = 0.0;

    amrex::Print() << std::setprecision(17)
                   << "ddf_norm_begin: reference=" << checkpoint_path
                   << " finest_level=" << finest_level
                   << " ncomp=" << Q << '\n';

    for (int lev = 0; lev <= finest_level; ++lev) {
        const std::string reference_name = amrex::MultiFabFileFullPrefix(
            lev, checkpoint_path, level_prefix, "f_old");
        MultiFab reference;
        VisMF::Read(reference, reference_name);

        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            reference.nComp() == Q,
            "DDF comparison found a reference component-count mismatch");
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            reference.boxArray() == boxArray(lev),
            "DDF comparison requires identical BoxArrays at every level");

        MultiFab reference_on_current(
            boxArray(lev), DistributionMap(lev), Q, 0);
        reference_on_current.ParallelCopy(
            reference, 0, 0, Q, IntVect(0), IntVect(0));

        if (stream_mode == 1) {
            const MultiFab& state = osi_state.at(lev);
            MultiFab difference(
                state.boxArray(), state.DistributionMap(), Q, 0);
            const bool has_fine =
                lev < finest_level;
            const Long valid_cells = state.boxArray().numPts();
            const Long active_cells =
                has_fine ? valid_cells - covered_cell_counts.at(lev)
                         : valid_cells;
            Real level_valid_linf = 0.0;
            Real level_valid_l1 = 0.0;
            Real level_valid_l2_sq = 0.0;
            Real level_active_linf = 0.0;
            Real level_active_l1 = 0.0;
            Real level_active_l2_sq = 0.0;
            Real level_active_ref_l2_sq = 0.0;

            DecodeOsiValid(state, osi_phase.at(lev), difference);
            for (MFIter mfi(difference, false); mfi.isValid(); ++mfi) {
                const Box bx = mfi.validbox();
                const auto diff = difference.array(mfi);
                const auto ref = reference_on_current.const_array(mfi);
                amrex::ParallelFor(
                    bx, Q,
                    [=] AMREX_GPU_DEVICE(
                        int i, int j, int k, int q) noexcept {
                        diff(i, j, k, q) -= ref(i, j, k, q);
                    });
            }

            for (int q = 0; q < Q; ++q) {
                const Real valid_linf =
                    difference.norminf(q);
                const Real valid_l1 =
                    difference.norm1(q);
                const Real valid_l2 =
                    difference.norm2(q);

                // Coordinate-level classification of the largest valid-cell
                // mismatch.  This is intentionally done before covered-cell
                // masking so that covered/interface status is reported too.
                Real max_diff = 0.0;
                IntVect max_iv(0);
                int max_covered = 0;
                int max_interface = 0;
                int max_physical = 0;
                int max_fab_edge = 0;
                Long mismatch_count = 0;
                Long mismatch_covered = 0;
                Long mismatch_interface = 0;
                Long mismatch_physical = 0;
                Long mismatch_fab_edge = 0;
                const Box domain = Geom(lev).Domain();
                for (MFIter mfi(difference, false);
                     mfi.isValid(); ++mfi) {
                    const Box bx = mfi.validbox();
                    FArrayBox host_diff(difference[mfi].box(),
                                        difference.nComp(),
                                        The_Pinned_Arena());
                    Gpu::dtoh_memcpy(host_diff.dataPtr(),
                                     difference[mfi].dataPtr(),
                                     host_diff.nBytes());
                    IArrayBox host_covered(
                        has_fine ? covered_mask.at(lev)[mfi].box() : bx,
                        1, The_Pinned_Arena());
                    IArrayBox host_interface(
                        has_fine ? interface_mask.at(lev)[mfi].box() : bx,
                        1, The_Pinned_Arena());
                    if (has_fine) {
                        Gpu::dtoh_memcpy(
                            host_covered.dataPtr(),
                            covered_mask.at(lev)[mfi].dataPtr(),
                            host_covered.nBytes());
                        Gpu::dtoh_memcpy(
                            host_interface.dataPtr(),
                            interface_mask.at(lev)[mfi].dataPtr(),
                            host_interface.nBytes());
                    }
                    const auto diff = host_diff.const_array();
                    const auto covered = host_covered.const_array();
                    const auto interface = host_interface.const_array();
                    const IntVect lo = bx.smallEnd();
                    const IntVect hi = bx.bigEnd();
                    for (int k = lo[2]; k <= hi[2]; ++k) {
                        for (int j = lo[1]; j <= hi[1]; ++j) {
                            for (int i = lo[0]; i <= hi[0]; ++i) {
                                const Real d = std::abs(diff(i, j, k, q));
                                if (d <= Real(1.e-12))
                                    continue;
                                ++mismatch_count;
                                const int cv = has_fine ? covered(i, j, k) : 0;
                                const int iv = has_fine ? interface(i, j, k) : 0;
                                const bool physical =
                                    (!Geom(lev).isPeriodic(0) &&
                                     (i == domain.smallEnd(0) || i == domain.bigEnd(0))) ||
                                    (!Geom(lev).isPeriodic(1) &&
                                     (j == domain.smallEnd(1) || j == domain.bigEnd(1))) ||
                                    (!Geom(lev).isPeriodic(2) &&
                                     (k == domain.smallEnd(2) || k == domain.bigEnd(2)));
                                const bool fab_edge =
                                    i == lo[0] || i == hi[0] ||
                                    j == lo[1] || j == hi[1] ||
                                    k == lo[2] || k == hi[2];
                                mismatch_covered += (cv != 0);
                                mismatch_interface += (iv != 0);
                                mismatch_physical += physical;
                                mismatch_fab_edge += fab_edge;
                                if (d > max_diff) {
                                    max_diff = d;
                                    max_iv = IntVect(AMREX_D_DECL(i, j, k));
                                    max_covered = cv != 0;
                                    max_interface = iv != 0;
                                    max_physical = physical;
                                    max_fab_edge = fab_edge;
                                }
                            }
                        }
                    }
                }
                amrex::Print()
                    << "ddf_mismatch_location: lev=" << lev
                    << " q=" << q
                    << " count_gt_1e-12=" << mismatch_count
                    << " covered=" << mismatch_covered
                    << " interface=" << mismatch_interface
                    << " physical_boundary=" << mismatch_physical
                    << " fab_edge=" << mismatch_fab_edge
                    << " max=" << max_diff
                    << " max_iv=" << max_iv
                    << " max_is_covered=" << max_covered
                    << " max_is_interface=" << max_interface
                    << " max_is_physical_boundary=" << max_physical
                    << " max_is_fab_edge=" << max_fab_edge
                    << '\n';

                if (has_fine) {
                    for (MFIter mfi(difference, false);
                         mfi.isValid(); ++mfi) {
                        const Box bx = mfi.validbox();
                        const auto diff =
                            difference.array(mfi);
                        const auto covered =
                            covered_mask.at(lev).const_array(mfi);
                        const auto interface =
                            interface_mask.at(lev).const_array(mfi);
                        amrex::ParallelFor(
                            bx,
                            [=] AMREX_GPU_DEVICE(
                                int i, int j, int k) noexcept {
                                const bool active = covered(i, j, k) == 0;
                                if (!active) {
                                    diff(i, j, k, q) = Real(0.0);
                                }
                            });
                    }
                }

                const Real active_linf =
                    difference.norminf(q);
                const Real active_l1 =
                    difference.norm1(q);
                const Real active_l2 =
                    difference.norm2(q);

                for (MFIter mfi(difference, false);
                     mfi.isValid(); ++mfi) {
                    const Box bx = mfi.validbox();
                    const auto values =
                        difference.array(mfi);
                    const auto ref =
                        reference_on_current.const_array(mfi);
                    if (has_fine) {
                        const auto covered =
                            covered_mask.at(lev).const_array(mfi);
                        const auto interface =
                            interface_mask.at(lev).const_array(mfi);
                        amrex::ParallelFor(
                            bx,
                            [=] AMREX_GPU_DEVICE(
                                int i, int j, int k) noexcept {
                                const bool active = covered(i, j, k) == 0;
                                values(i, j, k, q) =
                                    active ? ref(i, j, k, q)
                                           : Real(0.0);
                            });
                    } else {
                        amrex::ParallelFor(
                            bx,
                            [=] AMREX_GPU_DEVICE(
                                int i, int j, int k) noexcept {
                                values(i, j, k, q) =
                                    ref(i, j, k, q);
                            });
                    }
                }
                const Real active_ref_l2 =
                    difference.norm2(q);
                const Real active_rel_l2 =
                    active_ref_l2 > 0.0
                        ? active_l2 / active_ref_l2
                        : 0.0;
                const Real valid_l1_mean =
                    valid_cells > 0
                        ? valid_l1 / Real(valid_cells)
                        : 0.0;
                const Real active_l1_mean =
                    active_cells > 0
                        ? active_l1 / Real(active_cells)
                        : 0.0;
                level_valid_linf =
                    std::max(level_valid_linf, valid_linf);
                level_valid_l1 += valid_l1;
                level_valid_l2_sq += valid_l2 * valid_l2;
                level_active_linf =
                    std::max(level_active_linf, active_linf);
                level_active_l1 += active_l1;
                level_active_l2_sq += active_l2 * active_l2;
                level_active_ref_l2_sq +=
                    active_ref_l2 * active_ref_l2;

                amrex::Print()
                    << "ddf_cell_norm_component: lev=" << lev
                    << " q=" << q
                    << " valid_linf=" << valid_linf
                    << " valid_l1=" << valid_l1
                    << " valid_l1_mean=" << valid_l1_mean
                    << " valid_l2=" << valid_l2
                    << " active_linf=" << active_linf
                    << " active_l1=" << active_l1
                    << " active_l1_mean=" << active_l1_mean
                    << " active_l2=" << active_l2
                    << " active_ref_l2=" << active_ref_l2
                    << " active_rel_l2=" << active_rel_l2
                    << '\n';
            }
            const Real level_valid_l2 =
                std::sqrt(level_valid_l2_sq);
            const Real level_active_l2 =
                std::sqrt(level_active_l2_sq);
            const Real level_active_ref_l2 =
                std::sqrt(level_active_ref_l2_sq);
            const Real level_active_rel_l2 =
                level_active_ref_l2 > 0.0
                    ? level_active_l2 / level_active_ref_l2
                    : 0.0;
            const Real level_valid_l1_mean =
                valid_cells > 0
                    ? level_valid_l1 / Real(valid_cells * Q)
                    : 0.0;
            const Real level_active_l1_mean =
                active_cells > 0
                    ? level_active_l1 / Real(active_cells * Q)
                    : 0.0;
            global_linf = std::max(global_linf, level_active_linf);
            global_l1 += level_active_l1;
            global_diff_l2_sq +=
                level_active_l2 * level_active_l2;
            global_ref_l2_sq +=
                level_active_ref_l2 * level_active_ref_l2;
            amrex::Print()
                << "ddf_cell_norm_level: lev=" << lev
                << " valid_cells=" << state.boxArray().numPts()
                << " valid_linf=" << level_valid_linf
                << " valid_l1=" << level_valid_l1
                << " valid_l1_mean=" << level_valid_l1_mean
                << " valid_l2=" << level_valid_l2
                << " active_linf=" << level_active_linf
                << " active_l1=" << level_active_l1
                << " active_l1_mean=" << level_active_l1_mean
                << " active_l2=" << level_active_l2
                << " active_ref_l2=" << level_active_ref_l2
                << " active_rel_l2=" << level_active_rel_l2
                << '\n';
            continue;
        }

        const MultiFab& current = f_old[lev];

        MultiFab difference(
            current.boxArray(), current.DistributionMap(), Q, 0);
        MultiFab::Copy(difference, current, 0, 0, Q, 0);
        MultiFab::Subtract(
            difference, reference_on_current, 0, 0, Q, 0);

        const Real level_linf =
            difference.norm0(0, Q, IntVect(0));
        const Real level_diff_l2 = difference.norm2(0, Q);
        Real level_diff_l1 = 0.0;
        for (int q = 0; q < Q; ++q) {
            level_diff_l1 += difference.norm1(q);
        }
        const Real level_ref_l2 = reference_on_current.norm2(0, Q);
        const Real level_rel_l2 =
            level_ref_l2 > 0.0 ? level_diff_l2 / level_ref_l2 : 0.0;

        global_linf = std::max(global_linf, level_linf);
        global_l1 += level_diff_l1;
        global_diff_l2_sq += level_diff_l2 * level_diff_l2;
        global_ref_l2_sq += level_ref_l2 * level_ref_l2;

        amrex::Print() << "ddf_norm_level: lev=" << lev
                       << " valid_cells=" << current.boxArray().numPts()
                       << " linf=" << level_linf
                       << " l1=" << level_diff_l1
                       << " l2=" << level_diff_l2
                       << " ref_l2=" << level_ref_l2
                       << " rel_l2=" << level_rel_l2 << '\n';

        for (int q = 0; q < Q; ++q) {
            const Real linf = difference.norminf(q);
            const Real diff_l2 = difference.norm2(q);
            const Real ref_l2 = reference_on_current.norm2(q);
            const Real rel_l2 =
                ref_l2 > 0.0 ? diff_l2 / ref_l2 : 0.0;
            Long mismatch_count = 0;
            Long mismatch_covered = 0;
            Long mismatch_interface = 0;
            Long mismatch_physical = 0;
            Long mismatch_fab_edge = 0;
            Real max_diff = 0.0;
            IntVect max_iv(0);
            const Box domain = Geom(lev).Domain();
            const bool has_fine = lev < finest_level;
            for (MFIter mfi(difference, false); mfi.isValid(); ++mfi) {
                const Box bx = mfi.validbox();
                FArrayBox host_diff(mfi.validbox(), difference.nComp(), The_Pinned_Arena());
                Gpu::dtoh_memcpy(host_diff.dataPtr(), difference[mfi].dataPtr(), host_diff.nBytes());
                const auto diff = host_diff.const_array();
                const auto covered = has_fine ? covered_mask.at(lev).const_array(mfi) : Array4<const int>{};
                const auto interface = has_fine ? interface_mask.at(lev).const_array(mfi) : Array4<const int>{};
                const IntVect lo = bx.smallEnd();
                const IntVect hi = bx.bigEnd();
                for (int k = lo[2]; k <= hi[2]; ++k)
                    for (int j = lo[1]; j <= hi[1]; ++j)
                        for (int i = lo[0]; i <= hi[0]; ++i) {
                            const Real d = std::abs(diff(i, j, k, q));
                            if (d <= Real(1.e-12))
                                continue;
                            ++mismatch_count;
                            const bool physical =
                                (!Geom(lev).isPeriodic(0) && (i == domain.smallEnd(0) || i == domain.bigEnd(0))) ||
                                (!Geom(lev).isPeriodic(1) && (j == domain.smallEnd(1) || j == domain.bigEnd(1))) ||
                                (!Geom(lev).isPeriodic(2) && (k == domain.smallEnd(2) || k == domain.bigEnd(2)));
                            const bool fab_edge = i == lo[0] || i == hi[0] || j == lo[1] || j == hi[1] || k == lo[2] || k == hi[2];
                            mismatch_covered += has_fine && covered(i, j, k) != 0;
                            mismatch_interface += has_fine && interface(i, j, k) != 0;
                            mismatch_physical += physical;
                            mismatch_fab_edge += fab_edge;
                            if (d > max_diff) {
                                max_diff = d;
                                max_iv = IntVect(AMREX_D_DECL(i, j, k));
                            }
                        }
            }
            amrex::Print() << "ddf_mismatch_location: lev=" << lev << " q=" << q
                           << " count_gt_1e-12=" << mismatch_count
                           << " covered=" << mismatch_covered
                           << " interface=" << mismatch_interface
                           << " physical_boundary=" << mismatch_physical
                           << " fab_edge=" << mismatch_fab_edge
                           << " max=" << max_diff << " max_iv=" << max_iv << '\n';
            amrex::Print() << "ddf_norm_component: lev=" << lev
                           << " q=" << q
                           << " linf=" << linf
                           << " l2=" << diff_l2
                           << " ref_l2=" << ref_l2
                           << " rel_l2=" << rel_l2 << '\n';
        }
    }

    const Real global_diff_l2 = std::sqrt(global_diff_l2_sq);
    const Real global_ref_l2 = std::sqrt(global_ref_l2_sq);
    const Real global_rel_l2 =
        global_ref_l2 > 0.0 ? global_diff_l2 / global_ref_l2 : 0.0;
    amrex::Print() << "ddf_norm_global: linf=" << global_linf
                   << " l1=" << global_l1
                   << " l2=" << global_diff_l2
                   << " ref_l2=" << global_ref_l2
                   << " rel_l2=" << global_rel_l2 << '\n'
                   << "ddf_norm_end\n";
}
