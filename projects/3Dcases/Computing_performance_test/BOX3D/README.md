# BOX3D：三维方腔流耗时分析算例

本算例基于 AMReX 实现三维方腔流的 AMR-LBM 计算。它的主要用途是记录和分析时间推进、粗细网格数据传输及重网格等阶段的耗时，为性能归因和后续优化提供依据。

仓库级目录说明、构建环境和通用编译流程见[根目录 README](../../../../README.md)；本文件仅说明 BOX3D 特有的性能统计口径与分析资料。

## 性能统计口径

程序在 `src/main.cpp` 中每推进 1000 步输出一个性能窗口。窗口结束后，累计计时器和统计计数都会重置，因此单条 `stepXXXX` 记录只对应其前一个 1000 步窗口，不能直接视为整个算例的累计耗时。

除 `compute_time`、`regrid_time` 与 `JaberCycle_time` 外，程序还输出下列三组统计信息。

### 阶段耗时：`perf(s)`

| 字段 | 含义 |
| --- | --- |
| `interp` | 粗网格向细网格填充幽灵单元及其 LBM 非平衡量缩放的总耗时。 |
| `collide` | LBM 碰撞阶段耗时。 |
| `stream` | LBM 迁移阶段耗时。 |
| `average` | 细网格向粗网格限制及其相关缩放的总耗时。 |
| `comm` | 同层数据通信耗时。 |
| `boundary` | 边界条件处理耗时。 |
| `swap` | 新旧分布函数数据交换耗时。 |
| `solv` | 当前 `JaberCycle2()` 的窗口总耗时。 |
| `total` | 包含重网格等外围工作的窗口总耗时。 |
| `MLUPS_solv` / `MLUPS_total` | 分别以 `solv` 和 `total` 为分母计算的性能指标。 |

### 粗细网格传输细分：`perf_detail(s)`

| 字段 | 含义 |
| --- | --- |
| `interp_scale` | 粗网格到细网格填充前的 LBM 非平衡量缩放。 |
| `interp_cache_build` | 网格建立或 regrid 后构造 direct coarse-to-fine 插值布局的主机端耗时。 |
| `interp_regrid_fill` | `RemakeLevel()` 为新网格布局调用 `FillDdfPatch()` 的耗时，归属于 regrid 而非时间推进中的 `interp`。 |
| `interp_fillpatch` | 当前 DDF 粗细网格填充实现的总耗时；可能是通用 patch 路径或 direct 路径。 |
| `average_alloc` / `average_copy` / `average_scale` | 分步 restriction 的缓冲、复制与非平衡量缩放耗时；融合模式下应为 0。 |
| `average_down` | 当前所选 restriction 路径的总耗时，包含 kernel 与结果回写。 |
| `average_fused` | 恢复宏观量、非平衡量缩放及 8 个 fine child 平均的融合 kernel 耗时。 |
| `average_restrict` | 拆分模式中，将已经缩放的 8 个 fine child 平均到 coarse parent 的耗时。 |
| `average_copyback` | 从按 fine 布局的粗化结果缓冲区回写真实 coarse `MultiFab` 的耗时，可能包含 MPI 通信。 |

这些细分计时会在测量点同步 GPU，因此适合在同一插桩构建中定位耗时来源；不应与未插桩运行的绝对吞吐直接比较。

## 运行时碰撞路径

`lbm.collide_mode` 在 host 侧选择 D3Q27 BGK 碰撞实现，不改变 AMR 网格、cell mask
或 `JaberCycle2()` 调度：

| 模式 | 实现 | 用途 |
| ---: | --- | --- |
| `0` | 原始逐方向读取路径 | 语义基线与回归对照。 |
| `1` | 将 27 个 DDF 缓存在寄存器并复用宏观量的专用路径 | 当前无体力、无 SGS 方腔流的默认性能路径。 |

受控单 GPU 的 1000 步 A/B 测试和 64 步 DDF 范数验证分别由
`scripts/submit_collide_ab.sh` 与 `scripts/submit_collide_norm.sh` 提交。当前实测数据、
统计窗口和寄存器压力限制见[性能分析文档](docs/performance_profiling.md#专用-bgk-碰撞路径job-575206)。

### 调用量与近似工作量：`perf_count`

`interp_cache_builds` 记录该统计窗口内 direct coarse-to-fine 插值布局的实际构建次数，需与 `interp_cache_build` 配合判断重建频率和单次成本。`interp_regrid_fill_calls` 则记录 regrid 期间重新填充 level 数据的次数。

| 字段 | 含义 |
| --- | --- |
| `fillghost_calls` | 粗细网格幽灵单元填充调用次数。 |
| `avgdown_calls` | 细网格向粗网格限制调用次数。 |
| `interp_scale_full_cells` | 若每次都缩放整层粗网格时的候选格点数。 |
| `interp_scale_cells` | 粗到细缩放实际处理的格点数。 |
| `interp_scale_launch_boxes` | 粗到细缩放实际启动的缓存工作箱数量。 |
| `average_scale_cells` | 细到粗缩放处理的近似格点数。 |
| `average_parent_cells` | restriction 实际处理的 coarse parent 数；用于核对 full 与 interface-only 工作量。 |
| `boundary_full_cells` | 若对每层全部 valid cell 启动边界 kernel 时的基准格点数。 |
| `boundary_launch_cells` | 实际物理边界工作箱覆盖的格点数；两者之比用于衡量边界 launch 裁剪效果。 |

## 深入分析资料

- [`FillDdfPatch()` 函数学习文档](src/FillDdfPatch函数学习文档.md)
- [性能分析流程、测量结果与图表](docs/performance_profiling.md)
- [AMR 网格通信、幽灵单元填充与粗细网格传输](docs/amr_grid_communication.md)

使用 TinyProfiler 深入定位 `FillPatchTwoLevels()` 等内部开销时，应将结果用于路径归因，而非与未插桩版本比较生产吞吐。

当前主循环调用 `JaberCycle2()`；其粗细网格传输、两层 ghost 策略和迁移边界保护见
[AMR 网格通信文档](docs/amr_grid_communication.md)。构建和数值验证必须在算例目标的 HPC
编译环境中进行。该算例的 `config/GNUmakefile` 当前指向 `amrex-26.06`，使用 C++20，
并要求 GCC 11 或更新版本。

## IDE 语义分析

`.clangd` 从 `config/compile_commands.json` 读取本算例的编译参数，并用主机模式近似解析
NVCC 编译单元。配置中的 CUDA 关键字定义及 `blockIdx` 同类型替身只供 clangd 使用，
不进入真实 NVCC 构建。仓库根工作区的 `--query-driver` 必须使用逗号分隔多个编译器路径，
否则 clangd 无法提取 GCC 11 标准库目录，并会从缺失 `<numbers>` 开始产生级联误报。

完整刷新数据库并检查 clangd 时，可在算例根目录执行：

```bash
CCDB_REQUIRE_FULL=1 CCDB_MERGE_OLD=0 ./scripts/compile.sh
clangd --check=src/main.cpp --compile-commands-dir=config --enable-config \
  '--query-driver=/home/HPCBase/compilers/gcc/*/bin/g++'
```
