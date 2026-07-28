#ifndef AmrCoreLBM_H_
#define AmrCoreLBM_H_

#include <AMReX_Config.H>
#include <AMReX_AmrCore.H>
#include <AMReX_BCRec.H>
#include <AMReX_ParmParse.H>
#include <AMReX_FillPatcher.H>
#include <AMReX_iMultiFab.H>

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
        int regrid_int = -1;
        int plot_int = -1;
        int begin_plot = 0;
        int chk_int = -1;
        int begin_step = 0;
        std::string chk_prefix = "chk";
        bool keep_latest_only = true;
        bool write_particles = true;
    };

    struct PerfStats {
        double interp = 0.0;
        double interp_cache_build = 0.0;
        long long interp_cache_builds = 0;
        double interp_regrid_fill = 0.0;
        long long interp_regrid_fill_calls = 0;
        double interp_scale = 0.0;
        double interp_fillpatch = 0.0;
        double collide = 0.0;
        double stream = 0.0;
        double average = 0.0;
        double average_alloc = 0.0;
        double average_copy = 0.0;
        double average_scale = 0.0;
        double average_down = 0.0;
        double average_fused = 0.0;
        double average_restrict = 0.0;
        double average_copyback = 0.0;
        double comm = 0.0;
        double boundary = 0.0;
        double swap = 0.0;
        long long fillghost_calls = 0;
        long long avgdown_calls = 0;
        long long interp_scale_full_cells = 0;
        long long interp_scale_cells = 0;
        long long interp_scale_launch_boxes = 0;
        long long interp_fillpatch_fine_cells = 0;
        long long interp_fillpatch_coarse_cells = 0;
        long long interp_fillpatch_boxes = 0;
        long long average_scale_cells = 0;
        long long average_parent_cells = 0;
        long long boundary_full_cells = 0;
        long long boundary_launch_cells = 0;
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
    void PrintMeshInfo();
    void PrintLbmParm();
    void ResetPerfStats();
    PerfStats const& GetPerfStats() const;
    double ComputeWeightedLatticeUpdatesPerCoarseStep() const;
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
    void CompareDdfCheckpoint(const std::string& checkpoint_path) const;
    const RuntimeParams& params() const {
        return params_;
    }
    int ghostCells() const {
        return nghost;
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
    // CUDA 扩展版设备 Lambda 要求包裹它的成员函数必须是 public 公有访问权限
    void RebuildCoarseFineCaches();
    void FindCentre();
    //********************************************************************//
    //                           lbm  function                            //
    //********************************************************************//
    void ComputeMacroLevel(int lev);
    void ComputeMacro();
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
    //********************************************************************//
    //                     Pure virtual function                          //
    //********************************************************************//
    void MakeNewLevelFromCoarse(int lev, amrex::Real time, const amrex::BoxArray& ba, const amrex::DistributionMapping& dm) override;
    void RemakeLevel(int lev, amrex::Real time, const amrex::BoxArray& ba, const amrex::DistributionMapping& dm) override;
    void ClearLevel(int lev) override;
    void MakeNewLevelFromScratch(int lev, amrex::Real time, const amrex::BoxArray& ba, const amrex::DistributionMapping& dm) override;
    void ErrorEst(int lev, amrex::TagBoxArray& tags, amrex::Real time, int ngrow) override;

  private:
    void BuildInterpDirectCache(int lev);

    std::string plot_file{"plt"};

    int nghost = 2;
    int particle_num = 1;
    // 0：禁用粗细网格 mask；1：逐单元 mask。
    int cf_mask_mode = 1;
    // 0：原始通用 BGK；1：无外力/无 SGS 的 D3Q27 寄存器复用路径。
    int collide_mode = 0;
    // 0：全区域分步；1：全区域融合；2：交界区域分步；3：交界区域融合。
    int average_mode = 1;
    // 0：AMReX FillPatchTwoLevels；1：FPinfo patch；2：直接写 fine ghost；
    // 3：直接插值并在同一 kernel 内完成粗 DDF 非平衡缩放。
    int cf_interp_mode = 0;

    amrex::Vector<amrex::BCRec> bcs;
    amrex::Vector<amrex::Real> err;

    amrex::Vector<amrex::MultiFab> velocity;
    amrex::Vector<amrex::MultiFab> density;
    amrex::Vector<amrex::MultiFab> vorticity;
    amrex::Vector<amrex::MultiFab> shear;
    amrex::Vector<amrex::MultiFab> force;
    amrex::Vector<amrex::MultiFab> f_new;
    amrex::Vector<amrex::MultiFab> f_old;
    // 全区域融合平均的结果缓冲区；回写前沿用细层的网格布局与数据归属。
    amrex::Vector<amrex::MultiFab> average_down_buffer;
    // 模式 2/3 使用的稀疏结果缓冲区，仅包含粗细交界条带内的粗层父单元。
    amrex::Vector<amrex::MultiFab> average_interface_buffer;
    // 将每个稀疏交界 Box 映射到包含其全部子单元的细层 Fab。
    amrex::Vector<amrex::Vector<int>> average_interface_fine_box;

    // covered_mask 值为 1 的网格单元，代表该单元被更精细一层网格 `lev+1` 覆盖；
    // 交界面掩码（interface_mask）指被细网格覆盖、且紧贴未被覆盖粗网格单元的单层网格条带。
    amrex::Vector<amrex::iMultiFab> covered_mask;
    amrex::Vector<amrex::iMultiFab> interface_mask;
    amrex::Vector<amrex::Long> covered_cell_counts;
    amrex::Vector<amrex::Long> interface_cell_counts;
    amrex::Vector<amrex::Vector<amrex::Vector<amrex::Box>>> interp_scale_work_boxes;
    // mode 2：按 fine Box 归属缓存 coarse stencil，并直接写对应 fine Fab。
    amrex::Vector<amrex::MultiFab> interp_direct_coarse_stage;
    amrex::Vector<amrex::Vector<amrex::Vector<amrex::Box>>> interp_direct_fine_boxes;
    amrex::Vector<amrex::Vector<amrex::Vector<int>>> interp_direct_fine_index;
    amrex::Vector<amrex::Vector<unsigned char>> interp_direct_needs_physical_fill;
    amrex::Vector<unsigned char> interp_direct_cache_ready;
    amrex::Vector<amrex::Vector<amrex::Vector<amrex::Box>>> boundary_work_boxes; //boundary_work_boxes[lev][ibox][iface] ibox：该层级中的网格块编号;iface：该网格块上的某个物理边界工作 Box

    amrex::Vector<amrex::Real> tau;

    amrex::Vector<amrex::IntVect> static_lo;
    amrex::Vector<amrex::IntVect> static_hi;

    amrex::Vector<std::unique_ptr<LagrangeParticleContainer>> particles;
    amrex::Vector<std::unique_ptr<AuxiliaryPointContainer>> particlesCp;
    amrex::Vector<amrex::RealVect> points;
    amrex::Vector<amrex::Long> regrid_tag_counts;
    PerfStats perf_stats;
    RuntimeParams params_;
};

#endif
