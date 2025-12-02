#ifndef AmrCoreLBM_H_
#define AmrCoreLBM_H_

#include <AMReX_Config.H>
#include <AMReX_AmrCore.H>
#include <AMReX_BCRec.H>
#include <AMReX_ParmParse.H>
#include <AMReX_FillPatcher.H>

#include <AMReX_AmrParticles.H>
#include <AMReX_Particles.H>

#include <limits>
#include <memory>
#include <string>

#include "LagrangeParticleContainer.H"

class AmrCoreLBM : public amrex::AmrCore
{

public:

    //********************************************************************//
    //                           constructor                              //
    //********************************************************************//
    AmrCoreLBM();
    ~AmrCoreLBM() override;
    AmrCoreLBM (amrex::Geometry const& level_0_geom, amrex::AmrInfo const& amr_info);
    AmrCoreLBM(AmrCoreLBM&& rhs) noexcept = default;
    AmrCoreLBM& operator= (AmrCoreLBM&& rhs) noexcept = default;
    AmrCoreLBM(const AmrCoreLBM& rhs) = delete;
    AmrCoreLBM& operator= (const AmrCoreLBM& rhs) = delete;

    //********************************************************************//
    //                           help function                            //
    //********************************************************************//
    void PrintMeshInfo();
    void PrintLbmParm();
    void ReadParameters();    
    void WriteVelocityFile(const int step, const amrex::Real time);
    void WriteParticleFile(const int step, const amrex::Real time);
    void WriteVorticityFile(const int step, const amrex::Real time);   
    //********************************************************************//
    //                           mesh function                            //
    //********************************************************************//
    void InitMesh(amrex::Real cur_time);
    void FillCoarsePatch(int lev, amrex::Real time, amrex::MultiFab& mf);
    void FillPatch(int lev, amrex::Real time, amrex::MultiFab& mf);
    void FillDdfPatch(int lev, amrex::Real time, amrex::MultiFab& mf);
    void FillMacroPatch(int lev, amrex::Real time, amrex::MultiFab& mf);    
    void RefineMesh(amrex::Real time);
    //********************************************************************//
    //                           lbm  function                            //
    //********************************************************************//
    void ComputeMacroLevel(int lev);
    void ComputeMacro();
    void ComputeVorticityLevel(int lev);
    void ComputeVorticity(amrex::Real time);
    void ComputeShearLevel(int lev);
    void ComputeShear();   
    void AverageDownValidLevel(int lev);
    void AverageDownValid();
    void AverageDownGhostLevel(int lev);
    void AverageDownGhost();
    void FillGhostLevel(int lev, amrex::Real time);
    void FillMacroGhostLevel(int lev, amrex::Real time);
    void FillForceGhostLevel(int lev, amrex::Real time);    
    void CommunicateLevel(int lev);
    void Boundary(int lev); 
    void Collide(int lev, int n);
    void Stream(int lev,  int n);
    void SwapLevel(int lev, int n);

    void InterpScale(int lev, int n);
    void AverageScale(int lev, int n);
    //********************************************************************//
    //                           ibm  function                            //
    //********************************************************************//
    void InitParticle(int lev);
    void InitCpPoint(int lev);
    void InterpForce(int lev);
    void SumForce(int lev);
    void ComputeParticle(int lev);
    void ReduceFxy(int lev, int step);
    void PrintParticleParm();
    void RedistributeParticle();
    void ComputeCp(int lev, int step);

    //********************************************************************//
    //                     IDF (Implicit Direct Forcing)                  //
    //********************************************************************//
    // 高层入口：在时间步内调用以执行隐式直接力求解与施加
    void ApplyIDF(int lev);
    
    // 可选：预组装 / 缓存矩阵（如果粒子拓扑不变，可减少重复开销）
    void IDF_AssembleMatrix(int lev);
    
    // 插值欧拉速度到拉格朗日点（D_I）
    void IDF_InterpolateEulerToLag(int lev);
    
    // 根据当前拉格朗日力（或解出的力密度）传播到欧拉网格（D_E）
    void IDF_SpreadLagToEuler(int lev);
    
    // 求解 A * x = b（A=D_I*D_E），更新拉格朗日力修正
    void IDF_SolveSystem(int lev);
    //********************************************************************//
    //                     Pure virtual function                          //
    //********************************************************************//
    void MakeNewLevelFromCoarse(int lev, amrex::Real time, const amrex::BoxArray& ba, const amrex::DistributionMapping& dm) override;
    void RemakeLevel(int lev, amrex::Real time, const amrex::BoxArray& ba, const amrex::DistributionMapping& dm) override;
    void ClearLevel(int lev) override;
    void MakeNewLevelFromScratch(int lev, amrex::Real time, const amrex::BoxArray& ba, const amrex::DistributionMapping& dm) override;
    void ErrorEst(int lev, amrex::TagBoxArray& tags, amrex::Real time, int ngrow) override;


private:

    std::string plot_file {"plt"};    

    int nghost = 4;
  
    amrex::Vector<amrex::BCRec> bcs;
    amrex::Vector<amrex::Real>  err;

    amrex::Vector<amrex::MultiFab> velocity;
    amrex::Vector<amrex::MultiFab> density;
    amrex::Vector<amrex::MultiFab> vorticity;
    amrex::Vector<amrex::MultiFab> shear;
    amrex::Vector<amrex::MultiFab> force;
    amrex::Vector<amrex::MultiFab> f_new;
    amrex::Vector<amrex::MultiFab> f_old;

    amrex::Vector<amrex::Real> tau;

    amrex::Vector<amrex::IntVect> static_lo;
    amrex::Vector<amrex::IntVect> static_hi;

    std::unique_ptr<LagrangeParticleContainer> mypc;

    // IDF 暂存数据（示例：拉格朗日点数量 NL，矩阵与向量）
    std::vector<amrex::Real> idf_rhs;          // b
    std::vector<amrex::Real> idf_sol;          // x (力密度或修正量)
    std::vector<amrex::Real> idf_interp_u;     // 插值后的欧拉速度 u_E
    std::vector<amrex::Real> idf_target_u;     // 目标速度（可由输入/常量设定）
    std::vector<amrex::Real> idf_force_lag;    // 当前拉格朗日力密度
    std::vector<amrex::Real> idf_A;            // 稠密矩阵 A (按行主序 NL*NL)
    int idf_matrix_size {0};                   // NL
    bool idf_matrix_built {false};             // 是否已构造 A
};


#endif