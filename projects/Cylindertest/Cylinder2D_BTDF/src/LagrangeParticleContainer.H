#ifndef LAGRANGEPARTICLECONTAINER_H_
#define LAGRANGEPARTICLECONTAINER_H_

#include <AMReX_MultiFab.H>
#include <AMReX_MultiFabUtil.H>
#include <AMReX_AmrParGDB.H>
#include <AMReX_Particles.H>
#include <AMReX_AmrParticles.H>
#include <AMReX_GpuContainers.H>

#include <vector>
#include <unordered_map>

#include "D2Q9.H"

struct PIdx {
    enum {
        fx = 0,
        fy,
        ufx,
        ufy,
        rho_interp, // 插值后的密度
        nattribs
    };
};

class MyParIter : public amrex::ParIter<0, 0, PIdx::nattribs> {
  public:
    using amrex::ParIter<0, 0, PIdx::nattribs>::ParIter;
    using RealVector = amrex::ParIter<0, 0, PIdx::nattribs>::ContainerType::RealVector;

    const std::array<RealVector, PIdx::nattribs>& GetAttribs() const {
        return GetStructOfArrays().GetRealData();
    }

    std::array<RealVector, PIdx::nattribs>& GetAttribs() {
        return GetStructOfArrays().GetRealData();
    }

    const RealVector& GetAttribs(int comp) const {
        return GetStructOfArrays().GetRealData(comp);
    }

    RealVector& GetAttribs(int comp) {
        return GetStructOfArrays().GetRealData(comp);
    }
};

//********************************************************************//
//                     继承AmrParticleContainer                       //
//********************************************************************//
// class LagrangeParticleContainer : public amrex::AmrParticleContainer<0, 0, PIdx::nattribs>
// {
// public:

//     LagrangeParticleContainer(amrex::AmrCore* amr_core)
//         :   amrex::AmrParticleContainer<0, 0, PIdx::nattribs>(amr_core)
//     {
//         amrex::Print()<<"Particle construct successed ..." << std:: endl;
//     }

//     ~LagrangeParticleContainer () override = default;

//     LagrangeParticleContainer ( const LagrangeParticleContainer &) = delete;
//     LagrangeParticleContainer& operator= ( const LagrangeParticleContainer & ) = delete;

//     LagrangeParticleContainer ( LagrangeParticleContainer && ) noexcept = default;
//     LagrangeParticleContainer& operator= ( LagrangeParticleContainer && ) noexcept = default;

//     void PrintParticleParm();

//     void InitParticle(int lev);

//     void MoveParticle();

//     void InterpForce(int lev, amrex::MultiFab& rho_lev, amrex::MultiFab& u_lev, amrex::MultiFab& force_lev);

//     void SaveFxy(int lev, int step);

//     void WriteParticle(int step);

// };

//********************************************************************//
//                     原版                                           //
//********************************************************************//
class LagrangeParticleContainer : public amrex::ParticleContainer<0, 0, PIdx::nattribs> {
  public:
    LagrangeParticleContainer(amrex::AmrCore* amr_core)
        : amrex::ParticleContainer<0, 0, PIdx::nattribs>(amr_core->GetParGDB()) {
        amrex::Print() << "Particle construct successed ..." << std::endl;
    }

    ~LagrangeParticleContainer() override = default;

    LagrangeParticleContainer(const LagrangeParticleContainer&) = delete;
    LagrangeParticleContainer& operator=(const LagrangeParticleContainer&) = delete;

    LagrangeParticleContainer(LagrangeParticleContainer&&) noexcept = default;
    LagrangeParticleContainer& operator=(LagrangeParticleContainer&&) noexcept = default;

    void PrintParticleParm();

    void InitParticle(int lev);

    void MoveParticle();

#if USE_MDF_TWO_STAGE
    void InterpForce(int lev, amrex::MultiFab& rho_lev, amrex::MultiFab& u_lev, amrex::MultiFab& force_lev, amrex::MultiFab& force_delta_lev);
#else
    void InterpForce(int lev, amrex::MultiFab& rho_lev, amrex::MultiFab& u_lev, amrex::MultiFab& force_lev);
#endif
    // 清零粒子力属性（用于 Multi-direct forcing 迭代前的初始化）
    void ZeroParticleForce(int lev);

    // 调试：输出粒子力总和
    void DebugPrintForceSum(int lev, int step, int iter);

    // 返回是否已达稳态
    bool SaveFxy(int lev, int step, amrex::MultiFab& u_lev, amrex::MultiFab& rho_lev);

    // 将稳态判断与圆柱表面压力系数统计拆分出的函数
    // 传入已归一化的阻力系数（Cd）用于收敛评估；返回是否已达稳态
    bool EvaluateConvergenceAndWriteCp(int lev, int step, amrex::Real cd_value, amrex::Real cl_value,
                                       amrex::MultiFab& u_lev, amrex::MultiFab& rho_lev);

    void WriteParticle(int step);

    //********************************************************************//
    //                     IDF 相关粒子操作                                //
    //********************************************************************//

    // 收集本进程粒子的位置和ID（用于矩阵组装）
    // 返回本进程的粒子数量
    int IDF_CollectParticleData(int lev,
                                std::vector<amrex::Real>& local_lag_pos,
                                std::vector<int>& local_lag_ids);

    // 在拉格朗日点上执行插值（欧拉->拉格朗日）
    void IDF_Interpolate(int lev,
                         amrex::MultiFab& u_lev,
                         amrex::MultiFab& rho_lev);

    // 从粒子属性中读取插值结果到 CPU vector
    void IDF_ReadInterpResults(int lev,
                               std::vector<amrex::Real>& local_interp_ux,
                               std::vector<amrex::Real>& local_interp_uy,
                               std::vector<amrex::Real>& local_interp_rho);

    // 将求解得到的力写回粒子属性
    void IDF_WriteForceToParticles(int lev,
                                   const std::vector<amrex::Real>& sol_x,
                                   const std::vector<amrex::Real>& sol_y,
                                   const std::unordered_map<int, int>& pid_to_global_idx);

    // 将拉格朗日力传播到欧拉网格
    void IDF_SpreadForce(int lev, amrex::MultiFab& force_lev);
};

#endif // LAGRANGEPARTICLECONTAINER_H_