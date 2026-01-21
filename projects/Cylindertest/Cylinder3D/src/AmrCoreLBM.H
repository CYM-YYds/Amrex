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
#include "AuxiliaryPointContainer.H"

class AmrCoreLBM : public amrex::AmrCore {

  public:
    struct RuntimeParams {
        // amr
        int regrid_int = -1;
        int plot_int = -1;
        int begin_plot = 0;
        // checkpoint
        int chk_int = -1;   // <=0 means disabled
        int begin_step = 0; // >0 to restart
        std::string chk_prefix = "chk";
        bool keep_latest_only = true;
        bool write_particles = true;
    };
    //********************************************************************//
    //                           constructor                              //
    //********************************************************************//
    AmrCoreLBM();
    ~AmrCoreLBM() override;
    AmrCoreLBM(amrex::Geometry const& level_0_geom, amrex::AmrInfo const& amr_info);
    AmrCoreLBM(AmrCoreLBM&& rhs) noexcept = default;
    AmrCoreLBM& operator=(AmrCoreLBM&& rhs) noexcept = default;
    AmrCoreLBM(const AmrCoreLBM& rhs) = delete;
    AmrCoreLBM& operator=(const AmrCoreLBM& rhs) = delete;

    //********************************************************************//
    //                           help function                            //
    //********************************************************************//
    void PrintGridLayout(const amrex::Vector<amrex::BoxArray>& ba, const amrex::Vector<amrex::DistributionMapping>& dm);
    void PrintMeshInfo();
    void PrintLbmParm();
    void ReadParameters();
    void WriteVelocityFile(const int step, const amrex::Real time);
    void WriteDensityFile(const int step, const amrex::Real time);
    void WriteParticleFile(const int step, const amrex::Real time);
    void WriteMultiParticleFile(const int step, const amrex::Real time);
    void WriteVorticityFile(const int step, const amrex::Real time);
    void WriteVelocityFile(const int step, const amrex::Real time, const int lev);
    void WriteVelocityFileWithParticle(const int step, const amrex::Real time);
    void WriteCheckpoint(int step, amrex::Real time) const;
    void ReadCheckpoint();
    // Accessor for runtime params
    const RuntimeParams& params() const {
        return params_;
    }
    //********************************************************************//
    //                           mesh function                            //
    //********************************************************************//
    void InitMesh(amrex::Real cur_time);
    void FillCoarsePatch(int lev, amrex::Real time, amrex::MultiFab& mf);
    void FillPatch(int lev, amrex::Real time, amrex::MultiFab& mf);
    void FillDdfPatch(int lev, amrex::Real time, amrex::MultiFab& mf);
    void FillMacroPatch(int lev, amrex::Real time, amrex::MultiFab& mf);
    void RefineMesh(amrex::Real time);
    void FindCentre();
    //********************************************************************//
    //                           lbm  function                            //
    //********************************************************************//
    void ComputeMacroLevel(int lev);
    void ComputeMacro();
    // Compute macroscopic fields only on a plane i = ix (slice) at level lev
    void ComputeMacroSlice(int lev, int ix);
    void ComputeVorticityLevel(int lev);
    void ComputeVorticity(amrex::Real time);
    void ComputeShearLevel(int lev);
    void ComputeShear();
    void AverageDownValidLevel(int lev, bool is_scale);
    void AverageDownValid();
    void AverageDownGhostLevel(int lev, bool is_scale);
    void AverageDownGhost();
    void FillGhostLevel(int lev, amrex::Real time, bool is_scale);
    void FillMacroGhostLevel(int lev, amrex::Real time);
    void FillForceGhostLevel(int lev, amrex::Real time);
    void CommunicateLevel(int lev);
    void Boundary(int lev);
    void Collide(int lev, int n);
    void Stream(int lev, int n);
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
    void SaveParticleVelocity(int lev, int step);
    void SaveParticlePosition(int lev, int step);
    void SaveParticleDistance(int lev, int step);
    void PrintParticleParm();
    void RedistributeParticle();
    void ComputeCp(int lev, int step);
    void MoveParticle(int lev, amrex::Real cur_time);
    void LubForceParticle(int lev, amrex::Real cur_time);
    // Dump force field to simple dat files (one per rank): columns i j k Fx Fy Fz
    void WriteForceDat(int lev, int step);
    //********************************************************************//
    //                     IDF (Implicit Direct Forcing)                  //
    //********************************************************************//
    // 高层入口：在时间步内调用以执行隐式直接力求解与施加
    void ApplyIDF(int lev);

    // 构建活跃欧拉点集合（所有拉格朗日点周围的有效欧拉节点去重后的并集）
    void BuildActiveEulerSet(int lev);

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

    void RecordMeanVelocityProfile(int step);

  private:
    std::string plot_file{"plt"};

    int nghost = 4;
    int particle_num = 2;

    amrex::Vector<amrex::BCRec> bcs;
    amrex::Vector<amrex::Real> err;

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

    amrex::Vector<std::unique_ptr<LagrangeParticleContainer>> particles;
    amrex::Vector<std::unique_ptr<AuxiliaryPointContainer>> particlesCp;
    amrex::Vector<amrex::RealVect> points;

    // Running mean accumulators for \bar{u}(y) on x=NX/2, y in [0,NH)
    amrex::Vector<amrex::Real> mean_u_sum_;
    int mean_ny_half_ = -1;
    bool mean_initialized_ = false;
    int mean_samples_ = 0;

    // Centralized runtime parameters
    RuntimeParams params_;

    // ================== IDF 隐式直接力数据结构 ==================
    // 3D 分离的力向量和速度向量
    std::vector<amrex::Real> idf_interp_u_x; // 插值后的欧拉速度 ux (NL_global)
    std::vector<amrex::Real> idf_interp_u_y; // 插值后的欧拉速度 uy (NL_global)
    std::vector<amrex::Real> idf_interp_u_z; // 插值后的欧拉速度 uz (NL_global)
    std::vector<amrex::Real> idf_interp_rho; // 插值后的密度 rho (NL_global)
    std::vector<amrex::Real> idf_target_u_x; // 目标速度 ux (通常为0，静止边界)
    std::vector<amrex::Real> idf_target_u_y; // 目标速度 uy
    std::vector<amrex::Real> idf_target_u_z; // 目标速度 uz
    std::vector<amrex::Real> idf_rhs_x;      // RHS: target_ux - interp_ux
    std::vector<amrex::Real> idf_rhs_y;      // RHS: target_uy - interp_uy
    std::vector<amrex::Real> idf_rhs_z;      // RHS: target_uz - interp_uz
    std::vector<amrex::Real> idf_sol_x;      // 求解的 fx (NL_global)
    std::vector<amrex::Real> idf_sol_y;      // 求解的 fy (NL_global)
    std::vector<amrex::Real> idf_sol_z;      // 求解的 fz (NL_global)

    // 全局矩阵 A = D_I × D_E (NL_global × NL_global，按行主序存储)
    std::vector<amrex::Real> idf_A;

    // A 的逆矩阵 A_inv (静止边界时预先计算，避免每步求解)
    std::vector<amrex::Real> idf_A_inv;
    bool idf_inverse_built{false}; // A_inv 是否已计算

    // 全局拉格朗日点位置（所有进程共享）
    std::vector<amrex::Real> idf_lag_pos_global; // [x0,y0,z0,x1,y1,z1,...] 大小 3*NL_global
    int idf_NL_global{0};                        // 全局拉格朗日点总数

    // 本地进程的拉格朗日点在全局数组中的起始偏移
    int idf_local_offset{0};
    int idf_local_NL{0}; // 本进程拥有的拉格朗日点数

    // MPI 通信参数（每个进程的粒子数量和偏移）
    // 在 BuildActiveEulerSet 中初始化，regrid 后更新
    std::vector<int> idf_all_NL; // 每个进程的粒子数量

    // 矩阵是否已构建
    bool idf_matrix_built{false};

    // 活跃欧拉点集合（仅最细层）与其数量 NE
    amrex::Vector<amrex::IntVect> idf_active_euler_nodes; // 最细层的欧拉节点坐标列表（去重）
    int idf_NE{0};                                        // 最细层的活跃欧拉节点数量

    // 欧拉节点到索引的映射（仅最细层）
    std::map<amrex::IntVect, int> idf_euler_index_map;

    // 全局欧拉点数量
    int idf_NE_global{0};
    std::vector<amrex::IntVect> idf_active_euler_nodes_global;
    std::map<amrex::IntVect, int> idf_euler_index_map_global;

    // 静止边界：几何与映射只需构建一次
    bool idf_geometry_built{false};
    // 本地粒子计数是否有效（regrid 后需要更新）
    bool idf_local_count_valid{false};

    // 稀疏 D_I（按行：每个拉格朗日点 i 的条目列表 <e_idx, wI>）与 D_E（按列：每个拉格朗日点 j 的条目列表 <e_idx, wE>）
    std::vector<std::vector<std::pair<int, amrex::Real>>> idf_DI_rows;
    std::vector<std::vector<std::pair<int, amrex::Real>>> idf_DE_cols;
};

#endif