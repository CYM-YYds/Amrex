#ifndef LAGRANGEPARTICLECONTAINER_H_
#define LAGRANGEPARTICLECONTAINER_H_

#include <AMReX_MultiFab.H>
#include <AMReX_MultiFabUtil.H>
#include <AMReX_AmrParGDB.H>
#include <AMReX_Particles.H>
#include <AMReX_AmrParticles.H>
#include <AMReX_GpuContainers.H>

#include <limits>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

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
    struct ConvergenceParams {
        int min_check_step = 30000;
        int check_interval = 100;
        int window_size = 40;
        int required_consecutive_windows = 5;
        int post_steady_hold_steps = 5000;
        amrex::Real cd_rel_span_tol = 5.0e-4;
        amrex::Real cd_slope_tol = 1.0e-6;
        amrex::Real cl_abs_mean_tol = 1.0e-5;
        amrex::Real cl_rms_tol = 5.0e-5;
        amrex::Real u_residual_tol = 1.0e-7;
        amrex::Real rho_residual_tol = 1.0e-7;
    };

    struct ConvergenceStats {
        int step = -1;
        amrex::Real cd = 0.0;
        amrex::Real cl = 0.0;
        amrex::Real cd_mean = 0.0;
        amrex::Real cd_rel_span = 0.0;
        amrex::Real cd_slope = 0.0;
        amrex::Real cl_mean = 0.0;
        amrex::Real cl_rms = 0.0;
        amrex::Real u_residual = std::numeric_limits<amrex::Real>::infinity();
        amrex::Real rho_residual = std::numeric_limits<amrex::Real>::infinity();
        int consecutive_windows = 0;
        int candidate_start_step = -1;
        bool window_ok = false;
        bool steady_candidate = false;
        bool steady_confirmed = false;
    };

    LagrangeParticleContainer(amrex::AmrCore* amr_core)
        : amrex::ParticleContainer<0, 0, PIdx::nattribs>(amr_core->GetParGDB()) {
        amrex::Print() << "Particle construct successed ..." << std::endl;
        ReadConvergenceParameters();
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
    void SaveFxy(int lev, int step);

    // 更新稳态监控状态；仅在完成持稳阶段后返回 true
    bool EvaluateConvergence(int lev, int step, amrex::MultiFab& u_lev, amrex::MultiFab& rho_lev);

    // 重载2：只插值密度
    void Interpolate_normal_offset_Rho(int lev, amrex::MultiFab& rho_lev);

    // 重载3：插值速度和密度（原有接口）
    void Interpolate_normal_offset(int lev,
                                   amrex::MultiFab& u_lev,
                                   amrex::MultiFab& rho_lev);

    // 计算圆柱表面压力系数分布（在距离壁面1.5Δx处采样）
    void ComputeCp(int lev, amrex::MultiFab& rho_lev, const std::string& filename);
    
    // 计算圆柱表面局部摩擦系数分布
    void ComputeCf(int lev, amrex::MultiFab& u_lev, const std::string& filename);

    // 通过力与压力分解来计算本地摩擦系数
    // 输出结果到文件，每行：theta, Cf(from_force), Cp
    void ComputeCf_from_force_pressure(int lev, const std::string& filename);

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
    // 可选参数 local_interp_ids: 若非空，同时返回粒子ID
    void IDF_ReadInterpResults(int lev,
                               std::vector<amrex::Real>& local_interp_ux,
                               std::vector<amrex::Real>& local_interp_uy,
                               std::vector<amrex::Real>& local_interp_rho,
                               std::vector<int>* local_interp_ids = nullptr);

    // 将求解得到的力写回粒子属性
    // 方案 C: 使用粒子ID直接计算全局索引（global_idx = pid - 1）
    void IDF_WriteForceToParticles(int lev,
                                   const std::vector<amrex::Real>& sol_x,
                                   const std::vector<amrex::Real>& sol_y,
                                   int idf_NL_global);

    // 将拉格朗日力传播到欧拉网格
    void IDF_SpreadForce(int lev, amrex::MultiFab& force_lev);

  private:
    void ReadConvergenceParameters();
    ConvergenceStats BuildConvergenceStats(int lev,
                                           int step,
                                           amrex::MultiFab& u_lev,
                                           amrex::MultiFab& rho_lev);
    void WriteConvergenceMonitor(const ConvergenceStats& stats) const;
    void WriteSteadySummaryHeader(std::ostream& os) const;

    ConvergenceParams convergence_params_;
    std::vector<int> sample_steps_;
    std::vector<amrex::Real> cd_samples_;
    std::vector<amrex::Real> cl_samples_;
    std::unique_ptr<amrex::MultiFab> prev_u_snapshot_;
    std::unique_ptr<amrex::MultiFab> prev_rho_snapshot_;
    mutable bool convergence_monitor_initialized_ = false;
    int consecutive_window_hits_ = 0;
    int steady_candidate_start_step_ = -1;
    bool steady_candidate_active_ = false;
    bool steady_confirmed_ = false;
    ConvergenceStats final_convergence_stats_;
};

#endif // LAGRANGEPARTICLECONTAINER_H_
