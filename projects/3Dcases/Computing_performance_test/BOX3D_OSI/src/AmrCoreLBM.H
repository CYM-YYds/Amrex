#ifndef AmrCoreLBM_H_
#define AmrCoreLBM_H_

#include <AMReX_Config.H>
#include <AMReX_Gpu.H>
#include <AMReX_Utility.H>
#include <AMReX_AmrCore.H>
#include <AMReX_BCRec.H>
#include <AMReX_ParmParse.H>
#include <AMReX_FillPatcher.H>
#include <AMReX_TagParallelFor.H>
#include <AMReX_iMultiFab.H>

#include <AMReX_AmrParticles.H>
#include <AMReX_Particles.H>

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include "LbmGridParams.H"

#include "LbmLattice.H"
#include "LagrangeParticleContainer.H"
#include "AuxiliaryPointContainer.H"
#include "OsiIndex.H"
#include "OsiCommunication.H"

// 多个实现文件共用的计时器与检查点格式常量；此处不保存运行状态。
namespace Box3dDetail {
inline constexpr char checkpoint_label_v1[] = "LBMCheckpoint";
inline constexpr char checkpoint_label_v2[] = "LBMCheckpointV2";
inline constexpr char checkpoint_layout_ab[] = "canonical_ab_two_array_v1";
inline constexpr char checkpoint_layout_osi[] = "canonical_osi_single_array_v1";
// 边界任务仅保存布局几何和本地索引；执行时取当前数组，允许 canonical 双缓冲交换。
struct BoundaryWorkTag {
    amrex::Box region;
    OSI::FabGeometry fab;
    int local_index;
    int phase_index;

    AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
    const amrex::Box& box() const noexcept { return region; }
};
struct BoundaryWorkCache {
    amrex::TagVector<BoundaryWorkTag> advance_tags;
    amrex::TagVector<BoundaryWorkTag> repair_tags;
    amrex::Long full_cells = 0;
    amrex::Long advance_cells = 0;
    amrex::Long repair_cells = 0;

    void clear() {
        advance_tags.undefine();
        repair_tags.undefine();
        full_cells = advance_cells = repair_cells = 0;
    }
};

// 正常 OSI 插值的布局任务；重网格临时 patch 继续使用独立的逐盒路径。
struct InterpolationBatchTag {
    amrex::Box region;
    amrex::Box target;
    OSI::FabGeometry fab;
    int stage_local_index;
    int fine_local_index;
    int phase_index;
    AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
    const amrex::Box& box() const noexcept { return region; }
};
struct InterpolationBatchCache {
    amrex::TagVector<amrex::Array4BoxTag<amrex::Real>> scale_tags;
    amrex::TagVector<InterpolationBatchTag> tags;
    void clear() {
        scale_tags.undefine();
        tags.undefine();
    }
};
// 每个标签对应逐盒 restriction 的一个 block；只保存几何和布局索引。
struct RestrictionBlockTag {
    amrex::Box region;
    OSI::FabGeometry fab;
    amrex::Long first_parent;
    int output_local_index;
    int fine_local_index;
    int phase_index;
};
inline constexpr int restriction_threads_per_block = 256;
inline constexpr int restriction_warp_size = 32;
inline constexpr int restriction_parents_per_block =
    restriction_threads_per_block / restriction_warp_size;

class ScopedPerfTimer {
  public:
    explicit ScopedPerfTimer(double& accum, bool enabled = true)
        : m_accum(accum), m_enabled(enabled),
          m_start(enabled ? amrex::second() : 0.0) {}

    ~ScopedPerfTimer() {
        // 关闭细分计时时不读取时钟、不同步 GPU，也不累加伪耗时。
        if (!m_enabled) { return; }
        amrex::Gpu::streamSynchronize();
        m_accum += amrex::second() - m_start;
    }

  private:
    double& m_accum;
    bool m_enabled;
    double m_start;
};
#ifdef AMREX_USE_CUDA
// A-B 与 OSI restriction 只共用启动形状和父单元坐标展开；数学操作由调用方提供。
template <typename ParentOperation>
void LaunchRestrictionWarps(const amrex::Box& box, ParentOperation operation) {
    constexpr int threads_per_block = restriction_threads_per_block;
    constexpr int warp_size = restriction_warp_size;
    constexpr int warps_per_block = threads_per_block / warp_size;
    static_assert(Q <= warp_size);
    const amrex::Long ncells = box.numPts();
    const int nblocks = static_cast<int>(
        (ncells + warps_per_block - 1) / warps_per_block);
    const auto lo = amrex::lbound(box);
    const int nx = box.length(0);
    const int ny = box.length(1);
    amrex::launch<threads_per_block>(
        nblocks, amrex::Gpu::Device::gpuStream(),
        [=] AMREX_GPU_DEVICE() noexcept {
            const int lane = threadIdx.x % warp_size;
            const int warp_in_block = threadIdx.x / warp_size;
            const amrex::Long icell =
                static_cast<amrex::Long>(blockIdx.x) * warps_per_block + warp_in_block;
            if (icell < ncells) {
                const int i = lo.x + static_cast<int>(icell % nx);
                const amrex::Long yz = icell / nx;
                const int j = lo.y + static_cast<int>(yz % ny);
                const int k = lo.z + static_cast<int>(yz / ny);
                operation(i, j, k, lane);
            }
        });
}
#endif
} // namespace Box3dDetail

class AmrCoreLBM : public amrex::AmrCore {

  public:
    struct RuntimeParams {
        int regrid_int = -1;
        int plot_int = -1;
        int begin_plot = 0;
        int perf_report_int = 1000;
        bool perf_osi_detail_timing = true; // OSI 嵌套细分计时会引入额外同步。
        int chk_int = -1;
        int begin_step = 0;
        std::string chk_prefix = "chk";
        bool keep_latest_only = true;
        bool write_particles = AMREX_SPACEDIM == 3;
    };

    // verification.* 中的一次性探针开关与计数；ReadParameters 读取一次，
    // 推进路径只做成员访问，不再每次调用 ParmParse。
    struct ProbeParams {
        bool regrid_fixture = false;     // 仅验证时按粗步切换受控加密区域。
        bool average_first = false;     // 首次 0 层界面平均前后导出快照
        bool ghost_first_fill = false;  // 首次 1 层粗到细 ghost 填充后导出
        int boundary_step = -1;         // 指定粗步 Boundary 后读取采样点 DDF
        int boundary_sample[3] = {1, 111, 111};
        int boundary_sample_q = Q - 1;
        int coarse_advance_count = 0;   // AdvanceLevel(0) 调用次数
        int level0_average_count = 0;   // 带缩放的 0 层界面平均次数
        int level1_fill_count = 0;      // 1 层 ghost 填充次数
    };

    struct PerfStats {
        double interp = 0.0;
        double interp_cache_build = 0.0;
        long long interp_cache_builds = 0;
        double interp_regrid_fill = 0.0;
        long long interp_regrid_fill_calls = 0;
        double interp_fillpatch = 0.0;
        double collide = 0.0;
        std::array<double, max_ref_level + 1> collide_level{};
        std::array<long long, max_ref_level + 1> collide_level_calls{};
        std::array<long long, max_ref_level + 1> collide_level_launch_cells{};
        double stream = 0.0;
        double average = 0.0;
        double average_copy = 0.0;
        double average_scale = 0.0;
        double average_down = 0.0;
        double average_fused = 0.0;
        double average_restrict = 0.0;
        double average_copyback = 0.0;
        double comm = 0.0;
        double osi_decode = 0.0;
        double osi_parallel_copy = 0.0;
        double osi_fillboundary = 0.0;
        double osi_encode = 0.0;
        double osi_mpi_pack = 0.0;
        double osi_mpi_pack_kernel = 0.0;
        double osi_mpi_dtoh = 0.0;
        double osi_mpi_wait = 0.0;
        double osi_mpi_htod = 0.0;
        double osi_mpi_unpack = 0.0;
        double osi_mpi_unpack_kernel = 0.0;
        double boundary = 0.0;
        double swap = 0.0;
        long long fillghost_calls = 0;
        long long avgdown_calls = 0;
        long long interp_fillpatch_fine_cells = 0;
        long long interp_fillpatch_coarse_cells = 0;
        long long interp_fillpatch_boxes = 0;
        long long average_scale_cells = 0;
        long long average_parent_cells = 0;
        long long boundary_full_cells = 0;
        long long boundary_launch_cells = 0;
    };

    // 显式指定阶段函数访问 canonical A-B 状态还是 twisted OSI 状态。
    // 诊断用 A-B oracle 在 stream_mode=1 时仍需选择 Canonical。
    enum class DdfLayout : std::uint8_t { Canonical,
                                          Osi };
    // 参数读取后固定的运行模式，避免生产推进依赖多个检测布尔量的组合。
    enum class RunMode : std::uint8_t {
        CanonicalAB,
        OsiProduction,
        OsiLockstep
    };

    //********************************************************************//
    //                           constructor                              //
    //********************************************************************//
    AmrCoreLBM();
    ~AmrCoreLBM() override;
    AmrCoreLBM(amrex::Geometry const& level_0_geom, amrex::AmrInfo const& amr_info,
               LbmGridParams grid);
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
    void WriteVorticityFile(const int step, const amrex::Real time);
    void WriteVelocityFile(const int step, const amrex::Real time, const int lev);
#if AMREX_SPACEDIM == 3
    void WriteVelocityFileWithParticle(const int step, const amrex::Real time);
#endif
    void WriteCheckpoint(int step, amrex::Real time) const;
    void ReadCheckpoint();
    void InitializeConvergence();
    [[nodiscard]] bool CheckConvergence(int step);
    const RuntimeParams& params() const {
        return params_;
    }
    const LbmGridParams& gridParams() const noexcept {
        return grid_;
    }
    int ghostCells() const {
        return nghost;
    }
    int streamMode() const {
        return run_mode == RunMode::CanonicalAB ? 0 : 1;
    }
    RunMode runMode() const noexcept {
        return run_mode;
    }
    int finestLevel() const {
        return finest_level;
    }
    bool osiReferenceEnabled() const {
        return run_mode == RunMode::OsiLockstep;
    }
    bool canonicalStateEnabled() const noexcept {
        return run_mode == RunMode::CanonicalAB ||
               run_mode == RunMode::OsiLockstep;
    }
    //********************************************************************//
    //                           mesh function                            //
    //********************************************************************//
    void InitMesh(amrex::Real cur_time);
    void FillMacroPatch(int lev, amrex::Real time, amrex::MultiFab& mf);
    void RefineMesh(amrex::Real time);
    // CUDA 扩展版设备 Lambda 要求包裹它的成员函数必须是 public 公有访问权限
    void RebuildCoarseFineCaches();
    // lev 表示当前粗层；函数填充 lev + 1 细层的 DDF ghost。
    void FillDdfGhostFromCoarse(int lev, amrex::Real time,
                                bool apply_scale);
    // lev 表示当前粗层；函数处理 lev + 1 的粗网格插值暂存区。
    void ApplyPhysicalBoundaryToInterpolationStage(
        int coarse_lev, amrex::MultiFab& stage,
        const amrex::Vector<unsigned char>& needs_physical_fill);
    void InterpolateDdfStageToFine(
        amrex::MultiFab& stage, amrex::MultiFab& fine,
        const amrex::Vector<amrex::Box>& work_boxes,
        const amrex::Vector<int>& fine_indices,
        bool fine_osi, std::uint64_t fine_phase,
        bool apply_scale, amrex::Real scale);
    void RemakeDdfState(int lev, amrex::Real time,
                        amrex::MultiFab& new_old_state);
    void RebuildCoarseFineMasks();
    void RebuildCoarseFineMasksForState(
        const amrex::Vector<amrex::MultiFab>& state);
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
    // 完整平均下传并仅修复被改写的粗层物理边界；可选检查点位于修复前。
    void AverageDownValid(const std::function<void()>& before_repair = {});
    // 只修复 covered 粗层 valid 物理边界，单层时不做处理。
    void RepairCurrentStatePhysicalBoundary();
    // repair_after_average=true 时跳过 uncovered，否则跳过 covered。
    // CUDA 扩展设备 Lambda 要求该实现辅助函数保持 public。
    void ApplyPhysicalBoundaryLevel(int lev, amrex::MultiFab& state,
                                    DdfLayout layout, bool repair_after_average);
    // fine_lev 表示当前细层；将 fine_lev 平均下传到 fine_lev - 1 粗层。
    void AverageDownInterfaceLevel(int fine_lev, bool is_scale);
    // lev 始终表示当前粗层并填充 lev + 1；is_scale 只控制非平衡 DDF 放缩。
    void FillGhostLevel(int lev, amrex::Real time, bool is_scale);
    void FillMacroGhostLevel(int lev, amrex::Real time);
    void FillForceGhostLevel(int lev, amrex::Real time);
    void CommunicateLevel(int lev,
                          DdfLayout layout = DdfLayout::Canonical);
    // OSI 版 FillBoundary：统一使用本地 raw copy 与 OSI-aware MPI。
    void FillBoundaryOsi(int lev);
    void Boundary(int lev, DdfLayout layout = DdfLayout::Canonical);
    void Collide(int lev, int n,
                 DdfLayout layout = DdfLayout::Canonical);
    void Stream(int lev, int n,
                DdfLayout layout = DdfLayout::Canonical);
    void SwapLevel(int lev, int n,
                   DdfLayout layout = DdfLayout::Canonical);
    void ValidateConfiguration() const;
    void AdvanceLevel(int lev);

    //********************************************************************//
    //                         手动诊断函数                               //
    //********************************************************************//
    // 以下函数可能触发 GPU 同步、全局归约或设备到主机回拷，
    // 正常时间推进不会自动调用。定位问题时可在目标阶段手动插入，
    // 性能测试前应再移除调用。
    // 检查所有 active level 的 DDF、密度和速度是否有限且物理可用。
    void ValidateInitializedState(const char* context = "Manual diagnosis");
    // 输出所有 level 的 valid/active DDF 总和及各方向分量和。
    void PrintDdfChecksums(int step);
    // 输出指定 level 的 DDF 分量和；Stream 后、Swap 前检查 f_new 时
    // 将 use_new 设为 true。
    void PrintLevelDdfChecksum(const char* stage, int lev,
                               bool use_new = false);
    // 输出粗到细插值缓存布局以及代表性 DDF 分量范数，仅用于 A-B 路径。
    void PrintInterpolationDiagnostics(const char* stage, int lev);
    // 输出最新一次 regrid 后的 level、Box 和标记单元数。
    void PrintRegridDiagnostics() const;
    // 统计 ErrorEst 实际读取的 valid 涡量及固定阈值超限比例，不修改 tag。
    void PrintRefinementCriteriaDiagnostics(int lev, amrex::Real time) const;
    // 必须在 Stream(lev, n) 之后、Boundary(lev) 之前调用，检查 f_new
    // 的非有限值、pull 源位置、ghost/Fab 边缘和粗细网格属性。
    void DiagnoseStreamResult(int lev, int n);
    // 必须在 Boundary(lev) 之后、SwapLevel(lev, n) 之前调用，
    // 检查 f_new 中非有限值的边界、interface 和相邻单元分布。
    void DiagnoseBoundaryResult(int lev);
    // 在指定粗步 Boundary 之后读取采样点 DDF，参数见 verification.boundary_probe_*。
    void ProbeBoundarySample(int lev, DdfLayout layout);
    // OSI A-B 对照、粒子 checksum 和 checkpoint DDF 对照仍由对应
    // verification.* 参数显式启用，它们不属于生产性能路径。
    void AdvanceAndCheckOsiReference(int lev, int step);
    void CheckOsiReferenceUncovered(int step, const char* stage);
    void CheckOsiReferenceLevel0(int step, const char* stage);
    void CheckOsiReferenceAllValid(int step, const char* stage);
    void CompareOsiReferenceStage(
        int lev, const char* stage,
        const amrex::MultiFab& reference, int compare_ngrow,
        bool exclude_physical_boundary = false,
        bool skip_covered = true);
    void CompareOsiStreamSources(int lev, const char* stage);
    void PrintParticleChecksums(int step) const;
    void CompareDdfCheckpoint(const std::string& checkpoint_path) const;

    //********************************************************************//
    //                           ibm  function                            //
    //********************************************************************//
    void InitParticle(int lev);
    void InitCpPoint(int lev);
    void PrintParticleParm();
    void RedistributeParticle();
#if AMREX_SPACEDIM == 3
    void WriteMultiParticleFile(const int step, const amrex::Real time);
    void InterpForce(int lev);
    void SumForce(int lev);
    void ComputeParticle(int lev);
    void ReduceFxy(int lev, int step);
    void SaveParticleVelocity(int lev, int step);
    void SaveParticlePosition(int lev, int step);
    void SaveParticleDistance(int lev, int step);
    void ComputeCp(int lev, int step);
    void MoveParticle(int lev, amrex::Real cur_time);
    void LubForceParticle(int lev, amrex::Real cur_time);
#endif

    //********************************************************************//
    //                     Pure virtual function                          //
    //********************************************************************//
    // 重网格新增细层时，按 ba/dm 分配该层数据，并从相邻粗层插值初始化 DDF。
    void MakeNewLevelFromCoarse(int lev, amrex::Real time, const amrex::BoxArray& ba, const amrex::DistributionMapping& dm) override;
    // 重网格更新已有层时，按新的 ba/dm 重建数据，保留旧层重叠区域的 DDF，并从粗层补齐新增区域。
    void RemakeLevel(int lev, amrex::Real time, const amrex::BoxArray& ba, const amrex::DistributionMapping& dm) override;
    // 移除网格层时，释放该层流场数据、OSI 通信任务和收发缓冲，并重置 OSI 相位。
    void ClearLevel(int lev) override;
    // 从头初始化网格时，按 ba/dm 分配该层数据，并根据初始物理条件设置流场。
    void MakeNewLevelFromScratch(int lev, amrex::Real time, const amrex::BoxArray& ba, const amrex::DistributionMapping& dm) override;
    // 根据当前流场及加密判据设置 tags，供 AMReX 生成或更新更细层网格。
    void ErrorEst(int lev, amrex::TagBoxArray& tags, amrex::Real time, int ngrow) override;

  private:
    void ValidateCheckpointLattice(const std::string& checkpoint_path) const;
    LbmGridParams grid_{};
    struct ConvergenceState {
        bool enabled = false;
        int check_int = 1000;
        int trend_int = 1000;
        int required = 3;
        int streak = 0;
        amrex::Real tolerance = 1.0e-12;
        std::unique_ptr<amrex::MultiFab> previous_check_velocity;
        std::unique_ptr<amrex::MultiFab> previous_trend_velocity;
    };
    ConvergenceState convergence_;

    std::array<amrex::Real, 3> MeasureLevel0VelocityChange(
        const amrex::MultiFab& previous) const;
    void BuildBoundaryWorkCache();
    void BuildOsiCommunicationRegionCache(int lev);
    void BuildInterpolationCache();
    void BuildAverageCache();
    void ProbeAverageInterfaceLevel(int lev, const char* stage);
    void ProbeFillGhostLevel(int lev);
    void ClearOsiTransferCaches();
    amrex::Interpolater* DdfInterpolater() const;
    void InitializeOsiLevel(int lev, const amrex::BoxArray& ba,
                            const amrex::DistributionMapping& dm);
    void InitializeNewLevelFromCoarse(int lev, amrex::Real time);
    void BuildInterpolationStage(
        int lev, const amrex::MultiFab& coarse, const amrex::MultiFab& fine,
        const amrex::Vector<amrex::Box>& work_boxes,
        const amrex::Vector<int>& fine_indices,
        amrex::MultiFab& stage,
        amrex::Vector<unsigned char>& needs_physical_fill) const;
    void InitializeDdfFromCoarse(
        int lev, amrex::Real time, const amrex::MultiFab& coarse,
        bool coarse_osi, std::uint64_t coarse_phase, amrex::MultiFab& fine,
        const amrex::Vector<amrex::Box>& requested_boxes = {},
        const amrex::Vector<int>& requested_indices = {});

  public:
    // nvcc extended device lambdas require their enclosing member functions to
    // be publicly accessible. These are implementation helpers, not user APIs.
    void DecodeOsiValid(const amrex::MultiFab& state,
                        std::uint64_t phase,
                        amrex::MultiFab& canonical) const;
    void AverageDownOsiValidLevel(int lev, bool is_scale);
    // lev 表示当前粗层；函数填充 lev + 1 细层的 OSI ghost。
    void FillOsiGhostFromCoarse(int lev, amrex::Real time,
                                bool apply_scale);
    // fine_lev 表示当前细层；将 fine_lev 平均下传到 fine_lev - 1 粗层。
    void AverageDownOsiLevel(int fine_lev, bool is_scale);
    // 缓存重载统一执行插值/平均搬运，不重建布局计划。
    template <bool SourceRaw>
    void ParallelCopyOsi(OSI::CpcCache<SourceRaw>& cache,
                         const OSI::PhaseShift3* phase_shifts,
                         OSI::CopyTiming timing = {});
    // OSI 版 ParallelCopy：按 CPC 逻辑交集把旧 raw 状态迁移到新 Fab 布局。
    void ParallelCopyOsi(int lev, const amrex::MultiFab& source,
                         std::uint64_t source_phase,
                         amrex::MultiFab& destination,
                         std::uint64_t destination_phase);

  private:
    void AdvanceOneLayout(int lev, DdfLayout layout);

    std::string plot_file{"plt"};

    int nghost = 2;
    int particle_num = 1;
    // 0：禁用粗细网格 mask；1：逐单元 mask。
    // 0：原始通用 BGK；1：无外力/无 SGS 的 D3Q27 寄存器复用路径。
    int collide_mode = 0;
    RunMode run_mode = RunMode::CanonicalAB;
    ProbeParams probe_;
    // 仅用于 OSI A/B 验证：以确定性非均匀 DDF 覆盖均匀初值，暴露错误 halo 来源。
    bool osi_verification_pattern = false;
    // 验证粒子 checkpoint/restart 时输出全局粒子数、坐标与 SoA 属性和。
    bool particle_checksum = false;
    // 0：全区域分步基准；1：交界区域融合。
    // 粗到细 DDF 插值：0 = ratio-2 三线性；1 = GPU 守恒线性；
    // 2 = GPU 单元二次插值。
    int interp_mode = 0;

    amrex::Vector<amrex::BCRec> bcs;
    amrex::Vector<amrex::Real> err;

    amrex::Vector<amrex::MultiFab> velocity;
    amrex::Vector<amrex::MultiFab> density;
    amrex::Vector<amrex::MultiFab> vorticity;
    amrex::Vector<amrex::MultiFab> shear;
    amrex::Vector<amrex::MultiFab> force;
    amrex::Vector<amrex::MultiFab> f_new;
    amrex::Vector<amrex::MultiFab> f_old;
    // OSI 生产状态只有这一份带两层保护区的 DDF；A-B 两数组仅由
    // osi_ab_check 显式启用。环形区间是每个 Fab 的完整 grown box。
    amrex::Vector<amrex::MultiFab> osi_state;
    amrex::Vector<std::uint64_t> osi_phase;
    // 每层本地 Fab 的共享偏移表；随布局建立、随相位更新。
    amrex::Vector<OSI::FabPhaseCache> osi_fab_phase_cache;
    amrex::Vector<OSI::FillBoundaryCache> osi_fillboundary_cache;
    // 仅在 AMReX 已确认 GPU-aware MPI 时，直接用 device staging buffer 通信。
    bool osi_mpi_device_direct = false;
    // host staging 实验路径：用独立 GPU stream 将批量 D2H/H2D 与本地
    // seam copy 和 MPI send 收尾重叠；默认关闭，便于与同步基线直接对照。
    bool osi_mpi_async_staging = false;
    // 非零时将每个 peer 的 host staging payload 按字节上限分块，
    // 形成 D2H/MPI/H2D 流水；默认 0 保留整块通信基线。
    int osi_mpi_pipeline_chunk_bytes = 0;
    // 模式 1 使用的稀疏结果缓冲区，仅包含粗细交界条带内的粗层父单元。
    amrex::Vector<amrex::MultiFab> average_interface_buffer;
    // 将每个稀疏交界 Box 映射到包含其全部子单元的细层 Fab。
    amrex::Vector<amrex::Vector<int>> average_interface_fine_box;

    // covered_mask 值为 1 的网格单元，代表该单元被更精细一层网格 `lev+1` 覆盖；
    // 交界面掩码（interface_mask）指被细网格覆盖、且紧贴未被覆盖粗网格单元的单层网格条带。
    amrex::Vector<amrex::iMultiFab> covered_mask;
    amrex::Vector<amrex::iMultiFab> interface_mask;
    amrex::Vector<amrex::Long> covered_cell_counts;
    amrex::Vector<amrex::Long> interface_cell_counts;
    // 按 fine Box 归属缓存 coarse stencil，并直接写对应 fine Fab。
    amrex::Vector<amrex::MultiFab> interp_direct_coarse_stage;
    // 每个 staging Box 与一个 fine work Box/Fab 一一对应。
    amrex::Vector<amrex::Vector<amrex::Box>> interp_direct_fine_boxes;
    amrex::Vector<amrex::Vector<int>> interp_direct_fine_index;
    amrex::Vector<amrex::Vector<unsigned char>> interp_direct_needs_physical_fill;
    amrex::Vector<unsigned char> interp_direct_cache_ready;
    // 按细层编号保存插值搬运缓存，按粗层编号保存平均写回缓存。
    // 几何、Array4 和收发缓冲只在布局重建时更新；相位偏移在执行时从对应层的共享表读取。
    amrex::Vector<OSI::CpcCache<true>> osi_interp_copy_cache;
    amrex::Vector<OSI::CpcCache<false>> osi_average_copy_cache;
    // 唯一来源为既有稀疏 staging/interface 布局；随 ClearOsiTransferCaches 失效。
    amrex::Vector<Box3dDetail::InterpolationBatchCache> osi_interpolation_work_cache;
    amrex::Vector<amrex::Gpu::DeviceVector<Box3dDetail::RestrictionBlockTag>>
        osi_restriction_block_tags;
    // 只由 BuildBoundaryWorkCache 建立；布局重建或清层时失效，不缓存 DDF 地址或相位值。
    amrex::Vector<Box3dDetail::BoundaryWorkCache> boundary_work_cache;

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
