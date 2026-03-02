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
#include <unordered_map>

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
    AmrCoreLBM(); /** * @brief 构造函数，默认构造函数 */
    ~AmrCoreLBM() override; /** * @brief 析构函数，使用override关键字确保正确覆盖基类析构函数 */
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
    // IDFData：为每个球体独立管理IDF所需的全局数据
    // 每个球体拥有自己的粒子位置、欧拉点、矩阵等，避免球体间数据冲突
    struct IDFData {
      // ===== 拉格朗日点相关 =====
      int local_NL{0};                              // 本进程粒子数
      std::vector<int> all_NL;                      // 所有进程的粒子数
      int NL_global{0};                             // 全局粒子总数
      int local_offset{0};                          // 本进程在全局数组中的偏移
      std::vector<amrex::Real> lag_pos_global;      // 全局粒子位置 [x,y,z,...] 大小3*NL_global
        
      // ===== 欧拉点相关 =====
      std::vector<amrex::IntVect> active_euler_nodes_global; // 全局活跃欧拉点
      std::map<amrex::IntVect, int> euler_index_map_global;   // 欧拉点→索引映射
      int NE_global{0};                             // 全局欧拉点数
      std::unordered_map<int, int> pid_to_idx;      // 原始 PID → 压缩索引 [0,NL)
        
      // ===== 插值速度和密度 =====
      std::vector<amrex::Real> interp_u_x;         // 插值速度 ux (NL_global)
      std::vector<amrex::Real> interp_u_y;         // 插值速度 uy (NL_global)
      std::vector<amrex::Real> interp_u_z;         // 插值速度 uz (NL_global)
      std::vector<amrex::Real> interp_rho;         // 插值密度 (NL_global)
        
      // ===== 目标速度（静止边界：全为0） =====
      std::vector<amrex::Real> target_u_x;         // 目标速度 ux
      std::vector<amrex::Real> target_u_y;         // 目标速度 uy
      std::vector<amrex::Real> target_u_z;         // 目标速度 uz
        
      // ===== 线性系统的右端项 (RHS) =====
      std::vector<amrex::Real> rhs_x;              // RHS_x: target_ux - interp_ux
      std::vector<amrex::Real> rhs_y;              // RHS_y: target_uy - interp_uy
      std::vector<amrex::Real> rhs_z;              // RHS_z: target_uz - interp_uz
        
      // ===== 求解的拉格朗日力 =====
      std::vector<amrex::Real> sol_x;              // 求解的力 fx (NL_global)
      std::vector<amrex::Real> sol_y;              // 求解的力 fy (NL_global)
      std::vector<amrex::Real> sol_z;              // 求解的力 fz (NL_global)
        
      // ===== 矩阵和求解 =====
      std::vector<amrex::Real> A;                  // 全局矩阵 A = D_I × D_E (NL_global²)
      std::vector<amrex::Real> A_inv;              // 矩阵逆 (静止边界预计算)
        
        
      // ===== 标志位（静止边界优化） =====
      bool geometry_built{false};      // 几何（欧拉点集、矩阵）是否已构建
      bool local_count_valid{false};   // 本地粒子计数是否有效（regrid后需更新）
      bool matrix_built{false};        // 矩阵是否已组装
      bool inverse_built{false};       // 矩阵逆是否已计算
    };

    // 高层入口：在时间步内调用以执行隐式直接力求解与施加
    void ApplyIDF(int lev);

    // 构建活跃欧拉点集合：针对第 particle_idx 个球体的拉格朗日点
    void BuildActiveEulerSet(int lev, int particle_idx, IDFData& data);

    // 组装该球体的全局矩阵 A = D_I × D_E
    void IDF_AssembleMatrix(int lev, int particle_idx, IDFData& data);

    // 插值欧拉速度到该球体的拉格朗日点（D_I）
    void IDF_InterpolateEulerToLag(int lev, int particle_idx, IDFData& data);

    // 根据该球体的拉格朗日力传播到欧拉网格（D_E）
    void IDF_SpreadLagToEuler(int lev, int particle_idx, IDFData& data);

    // 求解该球体的线性系统 A * x = b（A=D_I*D_E）
    void IDF_SolveSystem(int lev, int particle_idx, IDFData& data);

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
    int particle_num = 1;

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

    // 为每个球体维护一个IDFData实例（支持多球体计算）
    std::vector<IDFData> idf_data;
    
    // 追踪IDF数据是否已初始化（在ApplyIDF第一次调用时初始化）
    bool idf_data_initialized{false};

    // Running mean accumulators for \bar{u}(y) on x=NX/2, y in [0,NH)
    amrex::Vector<amrex::Real> mean_u_sum_;
    int mean_ny_half_ = -1;
    bool mean_initialized_ = false;
    int mean_samples_ = 0;

    // Centralized runtime parameters
    RuntimeParams params_;
};

#endif