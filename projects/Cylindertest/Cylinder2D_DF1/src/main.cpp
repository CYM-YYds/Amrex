#include <iostream>

#include <AMReX.H>
#include <AMReX_BLProfiler.H>
#include <AMReX_ParallelDescriptor.H>
#include <AMReX_Utility.H>

#include "AmrCoreLBM.H"

using namespace amrex;

void RohdeCycle(int lev, amrex::Real cur_time, AmrCoreLBM& lid);
void JaberCycle(int lev, amrex::Real cur_time, AmrCoreLBM& lid);

int main(int argc, char* argv[]) {
    amrex::Initialize(argc, argv);

    {
        const Real start_time = amrex::second();

        int max_step;
        amrex::Real stop_time;

        {
            amrex::ParmParse pp;
            pp.query("max_step", max_step);
            pp.query("stop_time", stop_time);
        }
        amrex::Geometry geom(
            amrex::Box({AMREX_D_DECL(0, 0, 0)}, {AMREX_D_DECL(NX - 1, NY - 1, NZ - 1)}),
            amrex::RealBox({AMREX_D_DECL(0., 0., 0.)}, {AMREX_D_DECL(nx, ny, nz)}),
            amrex::CoordSys::cartesian,
            {AMREX_D_DECL(0, 0, 0)});
        amrex::AmrInfo info{
            1,             // verbose
            max_ref_level, // max_level
            amrex::Vector<amrex::IntVect>{(size_t)max_ref_level + 1, {AMREX_D_DECL(2, 2, 2)}},
            amrex::Vector<amrex::IntVect>{(size_t)max_ref_level + 1, {AMREX_D_DECL(32, 32, 32)}},
            amrex::Vector<amrex::IntVect>{(size_t)max_ref_level + 1, {AMREX_D_DECL(128, 128, 128)}}};

        AmrCoreLBM lid(geom, info);
        const auto& runtime = lid.params();

        amrex::Real cur_time = runtime.begin_step * dt_0;

        if (runtime.begin_step > 0) {
            lid.ReadCheckpoint();
            lid.PrintMeshInfo();
            lid.PrintLbmParm();
            lid.PrintParticleParm();
            amrex::Print() << "[Checkpoint] Restarted at step=" << runtime.begin_step
                           << ", time=" << cur_time << "\n";
        } else {
            lid.InitMesh(cur_time);
            lid.PrintMeshInfo();
            lid.PrintLbmParm();
            lid.InitParticle(max_ref_level);
            lid.PrintParticleParm();
        }

        int start_step = runtime.begin_step + 1;
        for (int step = start_step; step <= max_step && cur_time < stop_time; step++) {
            amrex::Print() << "STEP " << step << "starts ..." << std::endl;

            if (step >= 0 && step % runtime.regrid_int == 0) {
                lid.AverageDownValid();
                lid.RefineMesh(cur_time);
                lid.RedistributeParticle();
            }

            // RohdeCycle(0, cur_time, lid);
            JaberCycle(0, cur_time, lid);

            // 每步保存 Cd/Cl 数据
            lid.ReduceFxy(max_ref_level, step);

            // 仅基于相邻两次 Cd 采样的相对变化判断是否停机
            bool steady = lid.EvaluateConvergence(max_ref_level, step);
            if (steady) {
                lid.ComputeCp(max_ref_level, step);
                lid.ComputeCf(max_ref_level, step);
                lid.ComputeCf_from_force_pressure(max_ref_level, step);
                lid.PrintMeshInfo();
                lid.ComputeMacro();
                lid.WriteVelocityFile(step, cur_time);
                amrex::Print() << "Cd-based convergence reached at step " << step
                               << ", final outputs written and simulation stopping." << std::endl;
                break;
            }

            cur_time += dt_0;

            if (step >= runtime.begin_int && step % runtime.plot_int == 0) {
                lid.PrintMeshInfo();
                lid.ComputeMacro();
                lid.WriteVelocityFile(step, cur_time);
            }

            if (runtime.chk_int > 0 && step % runtime.chk_int == 0) {
                lid.WriteCheckpoint(step, cur_time);
            }
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

// 边界加密不加密都可以
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
        lid.FillGhostLevel(lev, cur_time);
    }

    lid.CommunicateLevel(lev);
    lid.Stream(lev, 2);
    lid.SwapLevel(lev, 2);

    if (lev < max_ref_level) {
        lid.AverageDownGhostLevel(lev);
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
        lid.AverageDownGhostLevel(lev);
    }
}

// 边界加密不加密都可以--Jaber循环加入非平衡态缩放
void JaberCycle(int lev, amrex::Real cur_time, AmrCoreLBM& lid) {
    amrex::Real dt = lid.Geom(lev).CellSizeArray()[0];

    if (lev < max_ref_level) {
        lid.FillGhostLevel(lev + 1, cur_time);
    }

    if (lev == max_ref_level) {
        lid.ComputeParticle(lev);
        lid.FillForceGhostLevel(lev, cur_time); // 加一个力的填充ghost就好了
    }

    lid.Boundary(lev);
    lid.Collide(lev, 4);
    lid.CommunicateLevel(lev);
    lid.Stream(lev, 4);
    lid.SwapLevel(lev, 4);

    if (lev < max_ref_level) {
        JaberCycle(lev + 1, cur_time, lid);
        lid.AverageDownGhostLevel(lev);
    }

    if (lev == coarsest_level) {
        return;
    }

    cur_time += dt;

    if (lev < max_ref_level) {
        lid.FillGhostLevel(lev + 1, cur_time);
    }

    if (lev == max_ref_level) {
        lid.ComputeParticle(lev);
        lid.FillForceGhostLevel(lev, cur_time); // 加一个力的填充ghost就好了
    }

    lid.Boundary(lev);
    lid.Collide(lev, 4);
    lid.CommunicateLevel(lev);
    lid.Stream(lev, 4);
    lid.SwapLevel(lev, 4);

    if (lev < max_ref_level) {
        JaberCycle(lev + 1, cur_time, lid);
        lid.AverageDownGhostLevel(lev);
    }
}

// 边界加密不加密都可以--Rohde循环加入非平衡态缩放
//  void RohdeCycle(int lev, amrex::Real cur_time, AmrCoreLBM& lid)
//  {
//      amrex::Real dt = lid.Geom(lev).CellSizeArray()[0];

//     if(lev == max_ref_level)
//     {
//         lid.ComputeParticle(lev);
//     }

//     lid.Boundary(lev);
//     lid.Collide(lev, 0);

//     if(lev < max_ref_level)
//     {
//         RohdeCycle(lev+1, cur_time, lid);
//     }

//     if(lev > coarsest_level)
//     {
//         lid.FillGhostLevel(lev, cur_time);
//         lid.InterpScale(lev, 4);
//     }

//     lid.CommunicateLevel(lev);
//     lid.Stream(lev, 2);
//     lid.SwapLevel(lev, 2);

//     if(lev < max_ref_level)
//     {
//         lid.AverageScale(lev, 4);
//         lid.AverageDownGhostLevel(lev);
//     }

//     if(lev == coarsest_level)
//     {
//         return;
//     }

//     cur_time += dt;

//     if(lev == max_ref_level)
//     {
//         lid.ComputeParticle(lev);
//     }

//     lid.Boundary(lev);
//     lid.Collide(lev, 0);

//     if(lev < max_ref_level)
//     {
//         RohdeCycle(lev+1, cur_time, lid);
//     }

//     lid.CommunicateLevel(lev);
//     lid.Stream(lev, 2);
//     lid.SwapLevel(lev, 2);

//     if(lev < max_ref_level)
//     {
//         lid.AverageScale(lev, 4);
//         lid.AverageDownGhostLevel(lev);
//     }
// }
