#include <iostream>
#include <chrono>
#include <array>
#include <cmath>

#include <AMReX.H>
#include <AMReX_BLProfiler.H>
#include <AMReX_ParallelDescriptor.H>
#include <AMReX_Utility.H>

#include "AmrCoreLBM.H"
#include "OsiAbVerification.H"

using namespace amrex;

void RohdeCycle(int lev, amrex::Real cur_time, AmrCoreLBM& lid); // 好像更适配cumulant_opt
void JaberCycle(int lev, amrex::Real cur_time, AmrCoreLBM& lid); // 更适配cumulant
void Cycle2(int lev, amrex::Real cur_time, AmrCoreLBM& lid,
            OsiAbVerification& verification);
void RohdeCycleMultiParticle(int lev, amrex::Real cur_time, AmrCoreLBM& lid);
void JaberCycleMultiParticle(int lev, amrex::Real cur_time, AmrCoreLBM& lid);

namespace {

struct InputConfig {
    int max_step;
    Real stop_time;
    Real reynolds_number;
    int max_level;
    Vector<int> n_cell;
    Vector<Real> prob_lo;
    Vector<Real> prob_hi;
    Vector<int> is_periodic;
    Vector<int> max_grid_size;
    Vector<int> blocking_factor_x;
    Vector<int> blocking_factor_y;
    Vector<int> blocking_factor_z;
};

InputConfig ReadInputConfig() {
    InputConfig config;

    ParmParse pp;
    pp.get("max_step", config.max_step);
    pp.get("stop_time", config.stop_time);

    ParmParse pp_lbm("lbm");
    pp_lbm.get("reynolds_number", config.reynolds_number);

    ParmParse pp_amr("amr");
    pp_amr.get("max_level", config.max_level);
    config.n_cell.resize(AMREX_SPACEDIM);
    pp_amr.getarr("n_cell", config.n_cell);
    const auto n_amr_levels = static_cast<std::size_t>(config.max_level + 1);
    config.max_grid_size.resize(n_amr_levels);
    config.blocking_factor_x.resize(n_amr_levels);
    config.blocking_factor_y.resize(n_amr_levels);
    config.blocking_factor_z.resize(n_amr_levels);
    pp_amr.getarr("max_grid_size", config.max_grid_size);
    pp_amr.getarr("blocking_factor_x", config.blocking_factor_x);
    pp_amr.getarr("blocking_factor_y", config.blocking_factor_y);
    pp_amr.getarr("blocking_factor_z", config.blocking_factor_z);

    ParmParse pp_geometry("geometry");
    config.prob_lo.resize(AMREX_SPACEDIM);
    config.prob_hi.resize(AMREX_SPACEDIM);
    config.is_periodic.resize(AMREX_SPACEDIM);
    pp_geometry.getarr("prob_lo", config.prob_lo);
    pp_geometry.getarr("prob_hi", config.prob_hi);
    pp_geometry.getarr("is_periodic", config.is_periodic);

    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        config.max_step >= 0, "max_step must be non-negative");
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        config.stop_time >= 0.0, "stop_time must be non-negative");
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        std::isfinite(config.reynolds_number) && config.reynolds_number > 0.0,
        "lbm.reynolds_number must be finite and positive");
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        config.max_level >= 0 && config.max_level <= max_ref_level,
        "amr.max_level must be within the compiled AMR level range");
    for (const int cells : config.n_cell) {
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(cells > 0, "amr.n_cell must be positive");
    }
    for (std::size_t lev = 0; lev < n_amr_levels; ++lev) {
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            config.max_grid_size[lev] > 0, "amr.max_grid_size must be positive");
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            config.blocking_factor_x[lev] > 0 &&
                config.blocking_factor_y[lev] > 0 &&
                config.blocking_factor_z[lev] > 0,
            "amr.blocking_factor_x/y/z must be positive");
    }
    for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            config.prob_hi[dir] > config.prob_lo[dir],
            "geometry.prob_hi must be greater than geometry.prob_lo");
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            config.is_periodic[dir] == 0 || config.is_periodic[dir] == 1,
            "geometry.is_periodic entries must be 0 or 1");
    }
    const Real dx_x = (config.prob_hi[0] - config.prob_lo[0]) / config.n_cell[0];
    const Real dx_y = (config.prob_hi[1] - config.prob_lo[1]) / config.n_cell[1];
    const Real dx_z = (config.prob_hi[2] - config.prob_lo[2]) / config.n_cell[2];
    const Real spacing_scale = std::max({Real(1.0), std::abs(dx_x),
                                         std::abs(dx_y), std::abs(dx_z)});
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        std::abs(dx_x - dx_y) <= 1.0e-12 * spacing_scale &&
            std::abs(dx_x - dx_z) <= 1.0e-12 * spacing_scale,
        "current LBM kernels require isotropic coarse cell spacing");

    return config;
}

} // namespace

int main(int argc, char* argv[]) {
    amrex::Initialize(argc, argv);

    {
        const Real start_time = amrex::second();

        const InputConfig input = ReadInputConfig();
        const int max_step = input.max_step;
        const Real stop_time = input.stop_time;
        int regrid_int, plot_int, begin_plot;
        int perf_report_int = 1000;
        int chk_int = -1;
        int begin_step = 0;
        std::string ddf_reference_checkpoint;
        {
            amrex::ParmParse pp_verify("verification");
            pp_verify.query(
                "ddf_reference_checkpoint", ddf_reference_checkpoint);
        }

        const int runtime_max_level = input.max_level;
        const auto& prob_lo = input.prob_lo;
        const auto& prob_hi = input.prob_hi;
        const auto& periodic_input = input.is_periodic;
        const IntVect n_cell(AMREX_D_DECL(input.n_cell[0], input.n_cell[1], input.n_cell[2]));
        const Real x_length = prob_hi[0] - prob_lo[0];
        const Real y_length = prob_hi[1] - prob_lo[1];
        const Real z_length = prob_hi[2] - prob_lo[2];
        const Real dx = x_length / input.n_cell[0];
        const LbmGridParams grid{
            input.n_cell[0], input.n_cell[1], input.n_cell[2],
            x_length, y_length, z_length, dx, dx, dx / rate, dx / rate,
            U0 * x_length / input.reynolds_number};
        const std::array<int, AMREX_SPACEDIM> is_periodic{
            AMREX_D_DECL(periodic_input[0], periodic_input[1], periodic_input[2])};

        amrex::Geometry geom(
            amrex::Box(IntVect::TheZeroVector(), n_cell - IntVect::TheUnitVector()),
            amrex::RealBox({AMREX_D_DECL(prob_lo[0], prob_lo[1], prob_lo[2])},
                           {AMREX_D_DECL(prob_hi[0], prob_hi[1], prob_hi[2])}),
            amrex::CoordSys::cartesian, is_periodic);

        const auto n_amr_levels = static_cast<std::size_t>(runtime_max_level + 1);
        // 当前 LBM 多层推进只支持逐级二倍细化。
        amrex::Vector<amrex::IntVect> ref_ratio(n_amr_levels, amrex::IntVect(2));
        amrex::Vector<amrex::IntVect> blocking_factor(n_amr_levels);
        amrex::Vector<amrex::IntVect> max_grid_size(n_amr_levels);
        for (std::size_t lev = 0; lev < n_amr_levels; ++lev) {
            blocking_factor[lev] = amrex::IntVect(
                input.blocking_factor_x[lev], input.blocking_factor_y[lev],
                input.blocking_factor_z[lev]);
            max_grid_size[lev] = amrex::IntVect(input.max_grid_size[lev]);
        }
        amrex::AmrInfo info{1, runtime_max_level, ref_ratio,
                            blocking_factor, max_grid_size};

        AmrCoreLBM lid(geom, info, grid);
        OsiAbVerification verification;
        begin_step = lid.params().begin_step;
        chk_int = lid.params().chk_int;
        regrid_int = lid.params().regrid_int;
        plot_int = lid.params().plot_int;
        begin_plot = lid.params().begin_plot;
        perf_report_int = lid.params().perf_report_int;

        amrex::Real cur_time = begin_step * grid.dt;

        if (begin_step > 0) {
            lid.ReadCheckpoint();
            lid.PrintMeshInfo();
            lid.PrintLbmParm();
            amrex::Print() << "[Checkpoint] Restarted at step=" << begin_step
                           << ", time=" << cur_time << "\n";
        } else {
            lid.InitMesh(cur_time);
            lid.PrintMeshInfo();
            lid.PrintLbmParm();
            if (lid.params().write_particles) {
                lid.InitParticle(max_ref_level);
                lid.InitCpPoint(max_ref_level);
                lid.PrintParticleParm();
            }
        }

        lid.ValidateConfiguration(); // 配置、层级、OSI 数组和模式是否允许计算

        lid.InitializeConvergence();
        const bool verification_enabled = lid.osiReferenceEnabled();

        float compute_time = 0.0f;
        float regrid_time = 0.0f;
        float JaberCycle_time = 0.0f;
        double weighted_updates_window = 0.0;
        int stats_steps = 0;
        lid.ResetPerfStats();
        int start_step = begin_step + 1;
        for (int step = start_step; step <= max_step && cur_time < stop_time; step++) {
            amrex::Print() << "STEP " << step << "starts ..." << std::endl;
            auto start_time_compute_time = std::chrono::high_resolution_clock::now();
            auto start_time_regrid_time = std::chrono::high_resolution_clock::now();
            // regrid_time_outer(me, f_array, indices, story);

            // 检测器只在显式配置的目标步骤介入生产生命周期。
            if (verification_enabled) {
                verification.BeforeRegrid(lid, step);
            }

            if (step >= 0 && regrid_int > 0 && step % regrid_int == 0) {
                // mode 1 在普通时间步只更新粗细交界区域；regrid 可能重新暴露
                // 被细网格覆盖的粗单元，因此重网格前先执行一次完整平均下传。
                if (lid.finestLevel() > 0) {
                    lid.AverageDownValid();
                    if (verification_enabled) {
                        verification.AfterAverageDown(lid, step);
                    }
                }
                // 完整平均下传后，重新施加当前态物理边界；covered 边界单元也要修复，
                // 因为它们可能在本次 regrid 后重新暴露或参与新细层插值。
                lid.RepairCurrentStatePhysicalBoundary();
                if (verification_enabled) {
                    verification.AfterRepair(lid, step);
                }
                if (lid.streamMode() == 0 && lid.params().write_particles) {
                    lid.FindCentre();
                }
                lid.RefineMesh(cur_time);
                if (verification_enabled) {
                    verification.AfterRefineMesh(lid, step);
                }
                if (lid.params().write_particles) {
                    lid.RedistributeParticle();
                }
            }

            auto end_time_regrid_time = std::chrono::high_resolution_clock::now();
            regrid_time += std::chrono::duration<float, std::milli>(end_time_regrid_time - start_time_regrid_time).count();
            weighted_updates_window += lid.ComputeWeightedLatticeUpdatesPerCoarseStep();
            ++stats_steps;

            /*---------------用于计算静止圆球绕流----------------------------------*/
            // RohdeCycle(0, cur_time, lid);

            auto start_time_JaberCycle = std::chrono::high_resolution_clock::now();
            Cycle2(0, cur_time, lid, verification);
            if (verification_enabled) {
                verification.AfterAdvance(lid, 0, step);
            }
            lid.PrintParticleChecksums(step);
            auto end_time_JaberCycle = std::chrono::high_resolution_clock::now();
            JaberCycle_time += std::chrono::duration<float, std::milli>(end_time_JaberCycle - start_time_JaberCycle).count();

            // lid.ReduceFxy(max_ref_level, step);
            /*--------------------------------------------------------------------*/

            /*---------------用于计算多颗粒自由运动----------------------------------*/
            // RohdeCycleMultiParticle(0, cur_time, lid);
            // JaberCycleMultiParticle(0, cur_time, lid);

            // lid.SaveParticlePosition(0, step);
            // lid.SaveParticleVelocity(0, step);
            // lid.SaveParticleDistance(0, step);
            /*--------------------------------------------------------------------*/
            auto end_time_compute_time = std::chrono::high_resolution_clock::now();
            compute_time += std::chrono::duration<float, std::milli>(end_time_compute_time - start_time_compute_time).count();
            cur_time += grid.dt;

            const bool converged = lid.CheckConvergence(step);

            // if(step >= 98000 && step <= 100000 && step % 100 == 0)
            // {
            //     lid.ComputeCp(max_ref_level, step);
            // }

            if (step % perf_report_int == 0) {
                const auto& perf = lid.GetPerfStats();
                double total_s = static_cast<double>(compute_time) / 1000.0;
                double solv_s = static_cast<double>(JaberCycle_time) / 1000.0;
                double total_mlups = 0.0;
                double solv_mlups = 0.0;

                if (total_s > 0.0) {
                    total_mlups = weighted_updates_window / total_s / 1.0e6;
                }
                if (solv_s > 0.0) {
                    solv_mlups = weighted_updates_window / solv_s / 1.0e6;
                }

                lid.PrintMeshInfo();

                std::cout << "step" << step << " compute_time: " << compute_time << " ms" << " regrid_time: " << regrid_time << " ms" << " JaberCycle_time: " << JaberCycle_time << " ms"
                          << std::endl;
                std::cout << "step" << step
                          << " perf(s): interp=" << perf.interp
                          << " collide=" << perf.collide
                          << " stream=" << perf.stream
                          << " average=" << perf.average
                          << " comm=" << perf.comm
                          << " osi_decode=" << perf.osi_decode
                          << " osi_parallel_copy=" << perf.osi_parallel_copy
                          << " osi_fillboundary=" << perf.osi_fillboundary
                          << " osi_encode=" << perf.osi_encode
                          << " osi_mpi_pack=" << perf.osi_mpi_pack
                          << " osi_mpi_pack_kernel=" << perf.osi_mpi_pack_kernel
                          << " osi_mpi_dtoh=" << perf.osi_mpi_dtoh
                          << " osi_mpi_wait=" << perf.osi_mpi_wait
                          << " osi_mpi_htod=" << perf.osi_mpi_htod
                          << " osi_mpi_unpack=" << perf.osi_mpi_unpack
                          << " osi_mpi_unpack_kernel=" << perf.osi_mpi_unpack_kernel
                          << " boundary=" << perf.boundary
                          << " swap=" << perf.swap
                          << " solv=" << solv_s
                          << " total=" << total_s
                          << " steps=" << stats_steps
                          << " MLUPS_solv=" << solv_mlups
                          << " MLUPS_total=" << total_mlups
                          << std::endl;
                for (int lev = 0; lev <= lid.finestLevel(); ++lev) {
                    const double level_seconds = perf.collide_level.at(lev);
                    const long long launch_cells =
                        perf.collide_level_launch_cells.at(lev);
                    const double launch_mlups =
                        level_seconds > 0.0
                            ? static_cast<double>(launch_cells) / level_seconds / 1.0e6
                            : 0.0;
                    std::cout << "step" << step
                              << " collide_level: lev=" << lev
                              << " seconds=" << level_seconds
                              << " calls=" << perf.collide_level_calls.at(lev)
                              << " launch_cells=" << launch_cells
                              << " launch_MLUPS=" << launch_mlups
                              << std::endl;
                }
                std::cout << "step" << step
                          << " perf_detail(s): interp_cache_build=" << perf.interp_cache_build
                          << " interp_regrid_fill=" << perf.interp_regrid_fill
                          << " interp_fillpatch=" << perf.interp_fillpatch
                          << " average_copy=" << perf.average_copy
                          << " average_scale=" << perf.average_scale
                          << " average_down=" << perf.average_down
                          << " average_fused=" << perf.average_fused
                          << " average_restrict=" << perf.average_restrict
                          << " average_copyback=" << perf.average_copyback
                          << std::endl;
                std::cout << "step" << step
                          << " perf_count: fillghost_calls=" << perf.fillghost_calls
                          << " interp_cache_builds=" << perf.interp_cache_builds
                          << " interp_regrid_fill_calls=" << perf.interp_regrid_fill_calls
                          << " avgdown_calls=" << perf.avgdown_calls
                          << " interp_fillpatch_boxes=" << perf.interp_fillpatch_boxes
                          << " interp_fillpatch_fine_cells=" << perf.interp_fillpatch_fine_cells
                          << " interp_fillpatch_coarse_cells=" << perf.interp_fillpatch_coarse_cells
                          << " average_scale_cells=" << perf.average_scale_cells
                          << " average_parent_cells=" << perf.average_parent_cells
                          << " boundary_full_cells=" << perf.boundary_full_cells
                          << " boundary_launch_cells=" << perf.boundary_launch_cells
                          << std::endl;

                compute_time = 0.0f;    // 改为float
                regrid_time = 0.0f;     // 改为float
                JaberCycle_time = 0.0f; // 改为float
                weighted_updates_window = 0.0;
                stats_steps = 0;
                lid.ResetPerfStats();
            }

            if (plot_int > 0 && step >= begin_plot && step % plot_int == 0) {
                lid.PrintMeshInfo();
                lid.ComputeMacro();
                lid.ComputeVorticity(cur_time);
                lid.WriteVelocityFile(step, cur_time);
                // lid.WriteDensityFile(step, cur_time);
                // lid.WriteVorticityFile(step, cur_time);
                // lid.ComputeCp(max_ref_level, step);
                // lid.WriteMultiParticleFile(step, cur_time);
                // lid.WriteVelocityFile(step, cur_time, max_ref_level);
                // lid.WriteParticleFile(step, cur_time);
                // lid.WriteVorticityFile(step, cur_time);
                // lid.WriteVelocityFileWithParticle(step, cur_time);
            }

            if (chk_int > 0 && step % chk_int == 0) {
                lid.WriteCheckpoint(step, cur_time);
            }
            if (converged) {
                amrex::Print() << "CONVERGED step=" << step << '\n';
                break;
            }
        }

        if (!ddf_reference_checkpoint.empty()) {
            lid.CompareDdfCheckpoint(ddf_reference_checkpoint);
        }

        amrex::Real end_total = amrex::second() - start_time;
        if (lid.Verbose()) {
            ParallelDescriptor::ReduceRealMax(end_total, ParallelDescriptor::IOProcessorNumber());
            amrex::Print() << "\nTotal Time: " << end_total << '\n';
        }
    }

    amrex::Finalize();

    return 0;
}

/*---------------用于计算静止圆球绕流----------------------------------*/
void RohdeCycle(int lev, amrex::Real cur_time, AmrCoreLBM& lid) {
    amrex::Real dt = lid.Geom(lev).CellSizeArray()[0];

    if (lev == max_ref_level) {
        lid.ComputeParticle(lev);
    }

    lid.Boundary(lev);
    lid.Collide(lev, 0);

    if (lev < max_ref_level) {
        RohdeCycle(lev + 1, cur_time, lid);
    }

    if (lev > coarsest_level) {
        // RohdeCycle 尚未按新的 FillGhostLevel 层级语义重构：这里传入当前层，
        // 但 FillGhostLevel 将 lev 解释为粗层并定位 lev + 1 细层。
        lid.FillGhostLevel(lev, cur_time, 0);
    }

    lid.CommunicateLevel(lev);
    lid.Stream(lev, 2);
    lid.SwapLevel(lev, 2);

    if (lev < max_ref_level) {
        lid.AverageDownInterfaceLevel(lev + 1, 0);
    }

    if (lev == coarsest_level) {
        return;
    }

    cur_time += dt;

    if (lev == max_ref_level) {
        lid.ComputeParticle(lev);
    }

    lid.Boundary(lev);
    lid.Collide(lev, 0);

    if (lev < max_ref_level) {
        RohdeCycle(lev + 1, cur_time, lid);
    }

    lid.CommunicateLevel(lev);
    lid.Stream(lev, 2);
    lid.SwapLevel(lev, 2);

    if (lev < max_ref_level) {
        lid.AverageDownInterfaceLevel(lev + 1, 0);
    }
}

/*---------------用于计算静止圆球绕流----------------------------------*/
void JaberCycle(int lev, amrex::Real cur_time, AmrCoreLBM& lid) {
    amrex::Real dt = lid.Geom(lev).CellSizeArray()[0];
    const int nghost = lid.ghostCells();

    if (lev < lid.finestLevel()) {
        lid.FillGhostLevel(lev, cur_time, 1);
    }

    // if(lev == max_ref_level)
    // {
    //     lid.ComputeParticle(lev);
    //     lid.FillForceGhostLevel(lev, cur_time);//加一个力的填充ghost就好了
    // }

    lid.Boundary(lev);
    lid.Collide(lev, nghost);
    lid.CommunicateLevel(lev);
    lid.Stream(lev, nghost);
    lid.SwapLevel(lev, nghost);

    if (lev < max_ref_level) {
        JaberCycle(lev + 1, cur_time, lid);
        lid.AverageDownInterfaceLevel(lev + 1, 1);
    }

    if (lev == coarsest_level) {
        return;
    }

    cur_time += dt;

    if (lev < lid.finestLevel()) {
        lid.FillGhostLevel(lev, cur_time, 1);
    }

    // if(lev == max_ref_level)
    // {
    //     lid.ComputeParticle(lev);
    //     lid.FillForceGhostLevel(lev, cur_time);//加一个力的填充ghost就好了
    // }

    lid.Boundary(lev);
    lid.Collide(lev, nghost);
    lid.CommunicateLevel(lev);
    lid.Stream(lev, nghost);
    lid.SwapLevel(lev, nghost);

    if (lev < max_ref_level) {
        JaberCycle(lev + 1, cur_time, lid);
        lid.AverageDownInterfaceLevel(lev + 1, 1);
    }
}

void Cycle2(int lev, amrex::Real cur_time, AmrCoreLBM& lid,
            OsiAbVerification& verification) {
    amrex::Real dt = lid.Geom(lev).CellSizeArray()[0];

    // if(lev == max_ref_level)
    // {
    //     lid.ComputeParticle(lev);
    //     lid.FillForceGhostLevel(lev, cur_time);//加一个力的填充ghost就好了
    // }

    // 1. 父层在本 coarse step 开始时为下一层填充一次 ghost；两个
    //    fine substeps 共享这次时间插值，与既有两层 Jaber 调度一致。
    if (lev < lid.finestLevel()) {
        lid.FillGhostLevel(lev, cur_time, 1);
        if (lid.osiReferenceEnabled()) {
            verification.AfterFirstFillGhost(lid, lev);
        }
    }

    // 2. 当前层推进一个时间步
    lid.AdvanceLevel(lev);

    // 3. 下一层用一半时间步连续推进两次
    if (lev < lid.finestLevel()) {
        Cycle2(lev + 1, cur_time, lid, verification);
        Cycle2(lev + 1, cur_time + dt / 2.0, lid, verification);

        // 4. 两个细步完成后，只平均一次
        lid.AverageDownInterfaceLevel(lev + 1, 1);
    }
}

/*---------------用于计算多颗粒自由运动----------------------------------*/
void RohdeCycleMultiParticle(int lev, amrex::Real cur_time, AmrCoreLBM& lid) {
    amrex::Real dt = lid.Geom(lev).CellSizeArray()[0];

    if (lev == max_ref_level) {
        lid.ComputeParticle(lev);
        // lid.FillForceGhostLevel(lev, cur_time);
        lid.LubForceParticle(lev, cur_time);
        lid.MoveParticle(lev, cur_time);
    }

    lid.Boundary(lev);
    lid.Collide(lev, 0);

    if (lev < max_ref_level) {
        RohdeCycleMultiParticle(lev + 1, cur_time, lid);
    }

    if (lev > coarsest_level) {
        // RohdeCycleMultiParticle 尚未按新的 FillGhostLevel 层级语义重构；
        // 这里保留旧调用，后续需要单独核对最细层调用边界。
        lid.FillGhostLevel(lev, cur_time, 0);
    }

    lid.CommunicateLevel(lev);
    lid.Stream(lev, 2);
    lid.SwapLevel(lev, 2);

    if (lev < max_ref_level) {
        lid.AverageDownInterfaceLevel(lev + 1, 0);
    }

    if (lev == coarsest_level) {
        return;
    }

    cur_time += dt;

    if (lev == max_ref_level) {
        lid.ComputeParticle(lev);
        // lid.FillForceGhostLevel(lev, cur_time);
        lid.LubForceParticle(lev, cur_time);
        lid.MoveParticle(lev, cur_time);
    }

    lid.Boundary(lev);
    lid.Collide(lev, 0);

    if (lev < max_ref_level) {
        RohdeCycleMultiParticle(lev + 1, cur_time, lid);
    }

    lid.CommunicateLevel(lev);
    lid.Stream(lev, 2);
    lid.SwapLevel(lev, 2);

    if (lev < max_ref_level) {
        lid.AverageDownInterfaceLevel(lev + 1, 0);
    }
}

/*---------------用于计算多颗粒自由运动----------------------------------*/
void JaberCycleMultiParticle(int lev, amrex::Real cur_time, AmrCoreLBM& lid) {
    amrex::Real dt = lid.Geom(lev).CellSizeArray()[0];
    const int nghost = lid.ghostCells();

    if (lev < max_ref_level) {
        lid.FillGhostLevel(lev, cur_time, 1);
    }

    if (lev == max_ref_level) {
        lid.ComputeParticle(lev);
        lid.FillForceGhostLevel(lev, cur_time); // 加一个力的填充ghost就好了
        lid.LubForceParticle(lev, cur_time);
        lid.MoveParticle(lev, cur_time);
    }

    lid.Boundary(lev);
    lid.Collide(lev, nghost);
    lid.CommunicateLevel(lev);
    lid.Stream(lev, nghost);
    lid.SwapLevel(lev, nghost);

    if (lev < max_ref_level) {
        JaberCycleMultiParticle(lev + 1, cur_time, lid);
        lid.AverageDownInterfaceLevel(lev + 1, 1);
    }

    if (lev == coarsest_level) {
        return;
    }

    cur_time += dt;

    if (lev < max_ref_level) {
        lid.FillGhostLevel(lev, cur_time, 1);
    }

    if (lev == max_ref_level) {
        lid.ComputeParticle(lev);
        lid.FillForceGhostLevel(lev, cur_time); // 加一个力的填充ghost就好了
        lid.LubForceParticle(lev, cur_time);
        lid.MoveParticle(lev, cur_time);
    }

    lid.Boundary(lev);
    lid.Collide(lev, nghost);
    lid.CommunicateLevel(lev);
    lid.Stream(lev, nghost);
    lid.SwapLevel(lev, nghost);

    if (lev < max_ref_level) {
        JaberCycleMultiParticle(lev + 1, cur_time, lid);
        lid.AverageDownInterfaceLevel(lev + 1, 1);
    }
}
