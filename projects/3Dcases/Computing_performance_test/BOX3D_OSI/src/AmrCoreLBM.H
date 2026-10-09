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

#include "LagrangeParticleContainer.H"
#include "AuxiliaryPointContainer.H"
#include "OsiIndex.H"
#include "OsiCommunication.H"

namespace OSI {
struct CommunicationTag {
    amrex::Array4<const amrex::Real> src;
    amrex::Array4<amrex::Real> dst;
    amrex::Box region;
    FabGeometry fab;

    [[nodiscard]] AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE const amrex::Box& box() const noexcept {
        return region;
    }
};

} // namespace OSI

// 多个实现文件共用的计时器与检查点格式常量；此处不保存运行状态。
namespace Box3dDetail {
inline constexpr char checkpoint_label_v1[] = "LBMCheckpoint";
inline constexpr char checkpoint_label_v2[] = "LBMCheckpointV2";
inline constexpr char checkpoint_layout_ab[] = "canonical_ab_two_array_v1";
inline constexpr char checkpoint_layout_osi[] = "canonical_osi_single_array_v1";
class ScopedPerfTimer {
  public:
    explicit ScopedPerfTimer(double& accum)
        : m_accum(accum), m_start(amrex::second()) {}

    ~ScopedPerfTimer() {
        amrex::Gpu::streamSynchronize();
        m_accum += amrex::second() - m_start;
    }

  private:
    double& m_accum;
    double m_start;
};
}

class AmrCoreLBM : public amrex::AmrCore {

  public:
    struct RuntimeParams {
        int regrid_int = -1;
        int plot_int = -1;
        int begin_plot = 0;
        int perf_report_int = 1000;
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
    void WriteMultiParticleFile(const int step, const amrex::Real time);
    void WriteVorticityFile(const int step, const amrex::Real time);
    void WriteVelocityFile(const int step, const amrex::Real time, const int lev);
    void WriteVelocityFileWithParticle(const int step, const amrex::Real time);
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
        return stream_mode;
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
    void FillCoarsePatch(int lev, amrex::Real time, amrex::MultiFab& mf);
    void FillPatch(int lev, amrex::Real time, amrex::MultiFab& mf);
    void FillMacroPatch(int lev, amrex::Real time, amrex::MultiFab& mf);
    void RefineMesh(amrex::Real time);
    // CUDA 扩展版设备 Lambda 要求包裹它的成员函数必须是 public 公有访问权限
    void RebuildCoarseFineCaches();
    // lev 表示当前粗层；函数填充 lev + 1 细层的 DDF ghost。
    void FillDdfGhostFromCoarse(int lev, amrex::Real time,
                                bool apply_scale);
    // lev 表示当前粗层；函数处理 lev + 1 的粗网格插值暂存区。
    void ApplyPhysicalBoundaryToInterpolationStage(int lev);
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
    // OSI 版 FillBoundary：统一选择本地 raw copy、OSI-aware MPI 或 canonical 回退。
    void FillBoundaryOsi(int lev);
    void CommunicateOsiLevelLocalDirect(int lev);
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
    // 必须在 Stream(lev, n) 之后、Boundary(lev) 之前调用，检查 f_new
    // 的非有限值、pull 源位置、ghost/Fab 边缘和粗细网格属性。
    void DiagnoseStreamResult(int lev, int n);
    // 必须在 Boundary(lev) 之后、SwapLevel(lev, n) 之前调用，
    // 检查 f_new 中非有限值的边界、interface 和相邻单元分布。
    void DiagnoseBoundaryResult(int lev);
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
    void BuildBoundaryWorkBoxes();
    void BuildOsiCommunicationRegionCache(int lev);
    void BuildInterpolationCache();
    void BuildAverageCache();
    void ProbeAverageInterfaceLevel(int lev, const char* stage);
    void ProbeFillGhostLevel(int lev);
    void BuildDirectInterpolationCache(int lev);
    amrex::Interpolater* DdfInterpolater() const;
    void InitializeOsiLevel(int lev, const amrex::BoxArray& ba,
                            const amrex::DistributionMapping& dm);
    void InitializeNewLevelFromCoarse(int lev, amrex::Real time);
    void InterpolateCanonicalToFine(int lev, amrex::Real time,
                                    amrex::MultiFab& fine,
                                    amrex::MultiFab& coarse_canonical);

  public:
    // nvcc extended device lambdas require their enclosing member functions to
    // be publicly accessible. These are implementation helpers, not user APIs.
    void DecodeOsiValid(const amrex::MultiFab& state,
                        std::uint64_t phase,
                        amrex::MultiFab& canonical) const;
    void ScaleCanonical(const amrex::MultiFab& density,
                        const amrex::MultiFab& velocity,
                        amrex::MultiFab& canonical, amrex::Real scale) const;
    void InterpolateOsiFinePatchFromCoarse(
        int lev, amrex::Real time,
        const amrex::BoxArray& fine_patch_ba,
        const amrex::DistributionMapping& patch_dm,
        amrex::MultiFab& destination);
    void AverageDownOsiValidLevel(int lev, bool is_scale);
    // lev 表示当前粗层；函数填充 lev + 1 细层的 OSI ghost。
    void FillOsiGhostFromCoarse(int lev, amrex::Real time,
                                bool apply_scale);
    // fine_lev 表示当前细层；将 fine_lev 平均下传到 fine_lev - 1 粗层。
    void AverageDownOsiLevel(int fine_lev, bool is_scale);
    void CommunicateOsiLevel(int lev);
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
    // 0 = canonical A-B 数值基准模式
    // 1 = grown-Fab OSI 模式；要求 collide_mode=1
    int stream_mode = 0;
    RunMode run_mode = RunMode::CanonicalAB;
    // 仅用于 OSI A/B 验证：以确定性非均匀 DDF 覆盖均匀初值，暴露错误 halo 来源。
    bool osi_verification_pattern = false;
    // 仅在显式启用时分配 A-B 两数组并逐步比较；生产 OSI 模式保持单一体积级 DDF。
    bool osi_ab_check = false;
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
    // canonical 通信区域缓存。第一组是 FillBoundary 实际可能读取的
    // owner-valid 源区，第二组是实际写入的 grown-ghost 目标区；两者均按
    // [level][global Fab index][disjoint Box] 索引。canonical 数据在回退通信
    // 调用内按完整 Q 分量临时创建，不作为成员长期占用显存。
    amrex::Vector<amrex::Vector<amrex::Vector<amrex::Box>>> osi_decode_boxes;
    amrex::Vector<amrex::Vector<amrex::Vector<amrex::Box>>> osi_encode_boxes;
    amrex::Vector<amrex::TagVector<OSI::LocalCopyTag>> osi_local_copy_tags;
    amrex::Vector<amrex::Vector<OSI::RemoteCopyTag>> osi_remote_copy_tags;
    // MPI 计划、偏移和 staging 缓冲随网格缓存重建，时间步内只复用。
    amrex::Vector<amrex::TagVector<OSI::MpiPackTag>> osi_mpi_pack_tags;
    amrex::Vector<amrex::TagVector<OSI::MpiUnpackTag>> osi_mpi_unpack_tags;
    amrex::Vector<amrex::Vector<std::size_t>> osi_mpi_send_offsets;
    amrex::Vector<amrex::Vector<std::size_t>> osi_mpi_recv_offsets;
    amrex::Vector<amrex::Vector<std::size_t>> osi_mpi_send_counts;
    amrex::Vector<amrex::Vector<std::size_t>> osi_mpi_recv_counts;
    amrex::Vector<amrex::Gpu::DeviceVector<amrex::Real>> osi_mpi_send_device;
    amrex::Vector<amrex::Gpu::DeviceVector<amrex::Real>> osi_mpi_recv_device;
    amrex::Vector<amrex::Gpu::PinnedVector<amrex::Real>> osi_mpi_send_host;
    amrex::Vector<amrex::Gpu::PinnedVector<amrex::Real>> osi_mpi_recv_host;
    bool osi_local_direct = false;
    // 粗到细插值阶段使用 OSI raw-to-canonical 的直接复制；不支持时回退旧路径。
    bool osi_parallel_copy = false;
    bool osi_mpi_direct = false;
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
    // OSI 粗到细插值中，coarse_stage::ParallelCopy 实际读取的粗层
    // canonical source 区域，按 [fine level][coarse Fab index][Box] 缓存。
    amrex::Vector<amrex::Vector<amrex::Vector<amrex::Box>>>
        osi_interp_decode_boxes;
    amrex::Vector<unsigned char> interp_direct_cache_ready;
    // 单 rank 插值目标与 OSI 粗层 raw source 的直接复制计划。
    amrex::Vector<amrex::TagVector<OSI::LocalCopyTag>>
        osi_interp_local_copy_tags;
    amrex::Vector<amrex::Vector<amrex::Vector<amrex::Box>>> boundary_work_boxes; // boundary_work_boxes[lev][ibox][iface] ibox：该层级中的网格块编号;iface：该网格块上的某个物理边界工作 Box

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
