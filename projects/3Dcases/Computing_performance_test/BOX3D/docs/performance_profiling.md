# BOX3D 性能分析

## 目的

这个算例用于测量递归 `JaberCycle2()` 路径中的粗细网格 AMR 传输开销。相关的数据路径如下：

```text
FillGhostLevel -> FillDdfGhostFromCoarse
                 -> direct staging（正常时间推进）
                 -> FPinfo temporary patch（RemakeLevel 布局迁移）
AverageDownGhostLevel -> 选定的 LBM restriction -> ParallelCopy
```

算例级的详细计时器会标出高层子阶段；重构布局迁移走 FPinfo temporary patch，
其临时对象和数据搬运会归入 regrid 插值计时。

## 构建与提交

`config/GNUmakefile` 当前已启用 AMReX TinyProfiler：

```make
TINY_PROFILE = TRUE
```

请在算例根目录下构建性能分析可执行文件，并提交一个单 GPU、1000 步的任务。这个算例当前面向 AMReX 26.06 和 C++20；在目标 HPC 编译环境中请使用 GCC 11 或更新版本：

```bash
./scripts/compile.sh
dsub -s ./scripts/submit_tiny_profile.sh
```

提交脚本需要 `main3d.gnu.TPROF.MPI.CUDA.ex`，会关闭绘图和 checkpoint 输出，并传入：

```text
tiny_profiler.device_synchronize_around_region=1
tiny_profiler.print_threshold=0.01
```

第一个选项会在 profiling 区域边界同步 GPU。它能为异步 CUDA kernel 提供更有意义的归因，但会增加测得的墙钟时间。不要把得到的 `JaberCycle_time` 直接和普通 `TINY_PROFILE = FALSE` 的运行结果比较。

提交前请先验证启动约定：

```bash
bash tests/test_tiny_profile_submit.sh
```

## 历史性能分析结果

已完成的 `569665-tiny-profile.log` 1000 步运行记录如下：

| 操作 | 时间 | 解释 |
|---|---:|---|
| `FillPatchTwoLevels` inclusive | 54.01 s | 主导的 Interp 子路径 |
| `CellConservativeLinear::interp()` | 28.54 s | 该历史版本中的保守型粗到细插值 |
| `FillPatchSingleLevel` inclusive | 21.19 s | 源填充、ghost 填充和物理边界工作 |
| `amrex::Copy()` | 13.15 s | restriction 前 fine 端临时数据拷贝 |
| `amrex::average_down()` inclusive | 8.98 s | fine 到 coarse 的 restriction |

这份 profile 早于当前 DDF 路径中对 `cell_bilinear_interp` 的选择，以及当前的双 ghost 流变化。因此它仍然只能作为旧的保守映射路径的证据，不能直接作为当前源码树的基准。

`FillPatchTwoLevels` 包装器本身的 exclusive 时间只有 0.413 s。因此优化应重点放在插值工作、边界/ghost 填充和 patch 粒度上，而不是包装器元数据。临时 `MultiFab` 分配也不是主要目标；在同一 1000 步窗口里，它的算例级计时只有 0.15 s。

TinyProfiler 的计数包含 regrid 工作。在 job `569665` 中，它记录了 7136 次 `FillPatchTwoLevels` 调用，而 JaberCycle 记录了 7000 次 `FillGhostLevel` 调用。多出来的 136 次来自 `RemakeLevel()` 等 regrid 路径。同样，7087 次 `average_down` 调用中包含了 87 次 regrid restriction，另外 7000 次来自 JaberCycle 调用。

## 当前全运行基线：Job 571393

`logs/submit/571393-out.log` 完成了单 GPU 的 64,000 步 cavity 算例。它包含 64 个独立的 1000 步窗口。下面的数值是这些窗口的累计和，而不是只看最终的 `step64000` 行。

| 数量 | 累计时间 | 占 `JaberCycle2` 的比例 |
|---|---:|---:|
| `JaberCycle2` | 10376.60 s | 100.00% |
| `Compute total` | 10451.08 s | - |
| `regrid_time` | 74.41 s | 0.72% |
| Collide | 3080.94 s | 29.69% |
| Interp | 2098.61 s | 20.22% |
| Average | 2049.56 s | 19.75% |
| Stream | 1763.04 s | 16.99% |
| Boundary | 882.91 s | 8.51% |
| Comm | 496.85 s | 4.79% |
| Swap | 4.34 s | 0.04% |

`Compute total` 的测量区间是从 regridding 之前立即开始，到 `JaberCycle2()` 之后立即结束。因此它等于 `regrid_time + JaberCycle_time`，再加上计算加权更新指标以及循环/计时器簿记的极小主机端开销。绘图输出不在这个区间内。在这次运行中，残差约为每个 1000 步窗口 1--2 ms。

`Interp + Average = 4148.17 s`，即 `JaberCycle2` 的 39.97%，是一个有用的派生传输小计。它不能再加到上面的阶段行里，因为它本身就是其中两项的组合。

详细的传输计时器是同步的嵌套计时器。它们的总和适合做归因分析，但并不是 `Interp` 或 `Average` 的严格分割：

| 嵌套操作 | 累计时间 |
|---|---:|
| `interp_fillpatch` | 1693.40 s |
| `interp_scale` | 435.95 s |
| `average_copy` | 700.81 s |
| `average_scale` | 741.09 s |
| `average_down` | 586.17 s |
| `average_alloc` | 14.68 s |

这次运行使用了 `TINY_PROFILE = TRUE`，因此其吞吐量只是 profiling 基线，而不是未同步的生产速度基准。该算例在全部 64 个窗口上报告的加权 `MLUPS_total` 为 370.13。

### 性能总览图

可从任意完整提交日志生成一个六面板总览：

```bash
python3 scripts/plot_run_performance.py logs/submit/571393-out.log \
  --output docs/571393_performance_overview.png
```

已提交的输出文件是 [`571393_performance_overview.png`](571393_performance_overview.png)。第四个面板使用 `average_scale_cells` 作为重复 AMR 传输工作的代理量，而不是当前有效网格单元的瞬时数量。在 job 571393 中，它与 Stream 时间的 Pearson 相关系数为 0.9994；这说明两者都随 AMR 覆盖范围变化，而不是 restriction 直接导致 Stream 变慢。

### Jaber A6 对比的适用范围

Jaber et al. 的 A6 cavity 测试与这个算例在大目标上相同：都是 `Re=1000`、`64^3`、D3Q27、双精度、四层 cavity 计算，并且每 32 个 coarse 步进行一次 regridding。两者都使用线性的 coarse-to-fine 空间插值。但它们并不是直接的性能对标对象：A6 是单 GPU、固定 `4^3` block、GPU 原生 octree 求解器，使用仅界面 restriction 和原地 shared-memory streaming。BOX3D 则使用 AMReX patch、通用 `FillPatchTwoLevels()`、coarse-level 的覆盖/界面 mask，以及双 MultiFab pull streaming。当前默认的 `average_mode=3` 使用的是针对 LBM 的融合 restriction，并基于缓存的 coarse-interface parent 列表；在 regridding 之前仍会执行完整的 fine-valid restriction。这些 mask 不是 Jaber 的 fine-level `cells_ID_mask`，而历史 job `571393` 也早于当前的仅界面路径。在比较 MLUPS 之前，应先匹配网格覆盖、马赫数、细化准则和 active-node 计数。

### 专用 BGK 碰撞路径：Job 575206

`lbm.collide_mode=1` 是针对当前 BOX3D 无体力、无 SGS 方腔流的 D3Q27 BGK 专用路径：将 27 个 DDF 一次读入线程局部数组，复用它们完成宏观量与碰撞写回，并把 `u^2` 公共项提前计算。原始 `collide_mode=0` 保留为基线；两者都使用现有的 cell mask、MultiFab 布局和同一个 JaberCycle2 调度。

Job `575206` 在同一张 GPU 上依次运行两个 mode，每个 mode 为 `step1000` 窗口：

| 指标 | mode 0 | mode 1 | 变化 |
|---|---:|---:|---:|
| `collide` | 50.8128 s | 25.3797 s | -50.05% |
| `JaberCycle2` / `solv` | 116.8631 s | 91.1620 s | -21.99% |
| `Compute total` | 117.8087 s | 92.1033 s | -21.82% |
| `MLUPS_solv` | 521.03 | 667.92 | +28.19% |

按当前一致的 MLUPS 口径换算，Collision 等效吞吐从 `1.198 GLUPS` 提升到 `2.399 GLUPS`；论文 A6 表中的对应估算值约为 `2.081 GLUPS`。两个 mode 的 332 条 regrid/mask 统计逐行一致。

为检查重排浮点运算是否改变结果，Job `575207` 进行了 64 步 valid-DDF checkpoint 对比：`rel_l2=1.0699e-15`、`linf=1.3878e-15`，且未观察到网格序列偏移。优化 kernel 的 `sm_80` 编译使用 190--202 个寄存器，无 stack/spill；寄存器增加是这条路径的主要占用率风险，应在其他 GPU 上重新测量。

## Boundary 工作盒实验：Job 572280

`Boundary()` 以前会在所有有效单元上启动，然后让 `fill_boundary()` 去拒绝内部单元。修订后的实现会在 mesh 构建/regrid 之后缓存彼此不相交的物理边界 Box 列表，并且只在这些 Box 上启动。job `572280` 是这版修改的单 GPU、1000 步运行；job `571805` 是紧接着的上一轮单 GPU 对比运行。

| 数量 | Job 571805 | Job 572280 | 变化 |
|---|---:|---:|---:|
| Boundary | 17.3817 s | 7.8342 s | -54.93%（2.219x 加速） |
| `JaberCycle2` | 187.1537 s | 161.3041 s | -13.81% |
| Compute total | 188.2410 s | 162.3385 s | -13.76% |
| `MLUPS_total` | 343.64 | 398.70 | +16.02% |

新的计数器报告 `boundary_full_cells=69,021,204,480` 和 `boundary_launch_cells=2,552,369,464`；因此 kernel 启动区域只有原始全有效单元区域的 3.698%，几何上减少了 96.302%。两次运行的传输工作计数接近（`interp_scale_cells` 约差 1.0%，而 `average_scale_cells` 约差 0.06%），但这并不是一个受控的独立 kernel 基准：网格演化和其他阶段时间也不同。Boundary 的 2.219x 改善应直接归因于这次实验；总时间变化应视为运行级观测结果，而不是纯粹的 Boundary 贡献。

这次修改也在双 GPU、64 步的 smoke job `572281` 中得到验证。测试过程中没有出现 AMReX abort，也没有 MPI/CUDA 失败；严格的数值一致性仍然需要 field norm 或参考 profile 对比。

## 饼图

可根据 64 个窗口的汇总计时数据生成三张饼图：

```bash
python3 scripts/plot_transfer_cost_pies.py
```

该脚本需要 `matplotlib`，并会写出 `docs/transfer_cost_pies.png`。整体图使用给定的 JaberCycle 总时长 7608.30 s。由于四舍五入，打印出来的两位小数类别加和为 7608.29 s。

Interp 图在与仅包含 JaberCycle 的 Interp 总时长比较之前，会先从旧的 DDF 填充子阶段中扣除 24.98 s 的 regridding 传输时间。Average 图保留了 3.30 s 的残差，用于临时对象销毁、循环开销和计时器边界工作，因此每个饼图都能闭合到其声明的总时长。

## 插值缩放工作盒：Job 572516

在每次调用 `FillPatchTwoLevels()` 之前，旧的统一 DDF 填充函数会对每个有效 coarse 单元都执行非平衡 DDF 重标定 kernel。修订后的实现会缓存 AMReX 的 `FPinfo.ba_crse_patch` 所表示的精确 coarse 源区域，把它们与 coarse BoxArray 求交，并在下一次 regrid 之前复用结果。这包括 `CellBilinear::CoarseBox()` 的 stencil halo 和周期性源偏移。`RemakeLevel()` 期间如果目标 BoxArray 不匹配，则会有意回退到整层缩放。

jobs `572280` 和 `572516` 是受控的单 GPU、1000 步运行。它们的网格演化、`average_scale_cells` 和边界计数器完全一致。

| 数量 | Job 572280 | Job 572516 | 变化 |
|---|---:|---:|---:|
| Interp scaling cells | 17,586,585,600 | 1,274,175,728 | -92.755% |
| `interp_scale` | 7.2384 s | 2.5620 s | -64.605%（2.825x 加速） |
| Interp total | 34.2778 s | 29.2789 s | -14.583% |
| `JaberCycle2` | 161.3041 s | 156.4781 s | -2.992% |
| Compute total | 162.3385 s | 157.5063 s | -2.977% |
| `MLUPS_total` | 398.70 | 410.93 | +3.068% |

优化后的运行启动了 290,497 个插值缩放工作盒。尽管启动次数增加了，但减少后的 DDF 重构工作带来了净收益。双 GPU、64 步的 smoke job `572517` 也成功完成了两次 regrid，没有出现 AMReX、MPI 或 CUDA 失败。smoke 运行只验证执行和通信安全；严格的数值一致性仍然需要 field norm 或参考 profile 对比。

## 融合 fine-to-coarse restriction：Job 572591

之前的 `AverageDownGhostLevel()` 会分配一个临时 fine `MultiFab`，拷贝所有 fine valid DDF，先在单独的 kernel 中缩放，再调用通用的 `amrex::average_down()`。修订后的路径会缓存一个 coarse 形状的缓冲区，每个 coarse parent 用一个 CUDA warp 来缩放并平均其 fine 子单元，然后通过一次 `ParallelCopy()` 写回真实的 coarse 层。保留回写是因为 fine 派生布局与 coarse 布局，或者 MPI 所有权可能不同。

job `572587`，模式 1，是修改前的基线。job `572591` 使用了融合 restriction。两者都是单 GPU、1000 步运行，传输和边界单元计数完全一致。

| 数量 | 572587 模式 1 | 572591 | 变化 |
|---|---:|---:|---:|
| Average total | 33.9745 s | 29.2931 s | -13.779% |
| `JaberCycle2` | 161.9143 s | 153.6465 s | -5.106% |
| Compute total | 163.0239 s | 154.7040 s | -5.103% |
| `MLUPS_total` | 397.02 | 418.37 | +5.378% |

在 job `572591` 中，`average_fused=27.2083 s`，`average_copyback=2.0667 s`；旧的分配、拷贝和 scale 字段都为零。选定的 q-lane warp kernel 可为 `sm_80` 编译，使用 56 个寄存器，没有 stack spill 或寄存器 spill。一个 child-lane 备选方案（job `572593`）使用了 194 个寄存器，并把 Average 提高到了 65.1121 s，因此被否决。

该版本对每个 fine valid covered 区域都执行 restriction，并保留了之前的 AMReX 语义。最终的双 GPU、64 步 smoke job `572595` 成功穿过两次 regrid，达到 `finest_level=2`，没有出现 AMReX、MPI、CUDA 或断言失败。

## 仅界面 restriction：Jobs 573417 和 573418

`lbm.average_mode` 用于选择 restriction 实现：

| 模式 | 区域 | 实现 |
|---:|---|---|
| 0 | 所有 fine valid cells | 使用 `f_new` 作为临时区的拆分 copy/scale/restrict |
| 1 | 所有 fine valid cells | 使用变粗后的 fine 缓冲区进行融合 scale/restrict |
| 2 | coarse-fine interface | 使用 `f_new` 作为临时区的拆分 sparse copy/scale/restrict |
| 3 | coarse-fine interface | 融合 sparse scale/restrict |

模式 2 和 3 使用同一个缓存的 sparse coarse-parent Box 列表。该列表会在 regridding 后重建，并且必须与 coarse `interface_mask` 具有完全相同的单元数量。正常时间步中的 restriction 只更新这个 collar；而在每次 regrid 前的现有 `AverageDownValid()` 调用，会在覆盖的 coarse 单元暴露之前执行所需的完整同步。

jobs `573417`（模式 2）和 `573418`（模式 3）都是单 GPU、1000 步运行。两者处理了 274,898,288 个 coarse parents 和 2,199,186,304 个 fine children。它们的 31 组 regrid mesh-statistics 序列完全一致。

| 数量 | 模式 2 | 模式 3 | 变化 |
|---|---:|---:|---:|
| Average total | 22.4954 s | 5.7458 s | -74.46% |
| `JaberCycle2` | 237.9553 s | 226.1978 s | -4.94% |
| Compute total | 239.9378 s | 228.3266 s | -4.84% |
| `MLUPS_total` | 269.75 | 283.47 | +5.09% |

模式 2 用 4.5648 s 将界面子单元拷贝到 `f_new`，用 3.5150 s 对它们进行缩放，用 13.6801 s 做 restriction，并用 0.7121 s 将结果拷回。模式 3 用 4.7930 s 跑融合 kernel，并用 0.9360 s 拷回结果。Average 的大幅下降使模式 3 成为了 `config/inputs` 中的默认值。

双 GPU、64 步 smoke job `573419` 也在两个 MPI rank 下完成，并且界面工作列表/mask 计数符合预期。这验证了 sparse-buffer 回写能跨所测试的 MPI 分解正常执行；但它并不能证明与模式 0 或 1 的严格 field norm 等价。

可在算例目录下用下面命令重新构建并提交同样的短性能配置：

```bash
./scripts/compile.sh
dsub -s ./scripts/submit_interp_scale_perf.sh
```

## 当前 coarse-to-fine 路径

正常时间推进使用 regrid 缓存的 fine ghost 工作盒：

```text
coarse_stage.ParallelCopy
  -> 必要时进行 coarse 物理边界填充
  -> 在所需 coarse stencil Box 上执行 average_scale
  -> 直接把 interp_bilinear_d3q 写入 fine ghost Box
  -> fine FillBoundary 和物理边界填充
```

缓存的几何信息为每个 coarse staging Box 包含一个物理边界标志。这样可以在后续时间步中去掉重复的主机端 Box/domain 检查，但不会去掉占主导的 DDF 数据搬运或 kernel（`ParallelCopy`、`average_scale`、插值和 fine `FillBoundary`）。

历史 jobs `574431` 和 `574438` 比较了已删除的 FPinfo 实验路径与当前 direct 路径。覆盖 0--2 层所有 27 个分量的 valid-cell DDF 字段比较结果为：

```text
global linf=0, l2=0, relative_l2=0
```

这确认了 direct 与此前 FPinfo 实验路径在该测试中 bitwise 一致。当前代码只保留
direct 的数据组织；进一步优化应分别测量 `ParallelCopy`、coarse 缩放、插值和 fine
ghost-fill 的开销。

## coarse stencil staging 的当前设计结论

在当前 BOX3D 的 coarse/fine 对齐条件下，一个合法 `work_box` 对应一个确定的
coarse stencil；不同合法 `work_box` 不会产生完全相同的 stencil。因此当前 direct
cache 不做 `(coarse_box, owner)` 全局去重，而是让每个 `work_box` 独立对应一个
staging Box/Fab。`fine_index` 唯一确定目标 Fab 的 `DistributionMap()` owner，
该 owner 用于构造 `coarse_stage` 的 `DistributionMapping`。

这项设计删除了无实际几何收益的候选 Box 搜索；它不等于合并部分重叠的 coarse
stencil，也不改变 `ParallelCopy`、coarse 缩放、插值或 fine `FillBoundary` 的工作量。
历史 job `574448` 仍可作为过去去重实验的记录，但不再作为当前实现的描述或优化依据。

## direct 插值缓存生命周期与计时归属

direct 路径的单层几何布局现由 `BuildDirectInterpolationCache()` 构造，并在
`RebuildCoarseFineCaches()` 中随 mask、边界工作区和 restriction 缓存一起失效和重建。
总入口只负责清理和编排：

```text
RebuildCoarseFineCaches
  -> RebuildCoarseFineMasks
  -> BuildBoundaryWorkBoxes
  -> BuildInterpolationCache
       -> BuildDirectInterpolationCache(lev)
  -> BuildRestrictionCache
```

`FillDdfGhostFromCoarse()` 仅在正常时间推进缺少缓存时延迟构建；
`RemakeLevel()` 布局不匹配时使用 temporary patch，不会构造或使用 direct cache。
`interp_cache_build` 和 `interp_cache_builds` 分别记录 direct 布局的构建总耗时和次数。

job `575341` 用已删除的 FPinfo 实验路径 checkpoint 对当前 direct 路径做了 64 步逐层、逐 DDF 回归，所有
27 个分量均得到 `linf=0, l2=0, relative_l2=0`。job `575343` 在同一 GPU、同一
可执行文件和输入下顺序运行 1000 步，结果为：

| 数量 | 当前 direct | 历史 FPinfo patch |
| --- | ---: | ---: |
| 时间推进插值 `interp` | 30.2840 s | 34.0167 s |
| 总计算时间 | 120.8544 s | 124.0695 s |
| `MLUPS_total` | 505.38 | 490.76 |
| 缓存构建 | 0.00464 s / 96 次 | 0 s / 0 次 |

当前 direct 路径在该次动态 AMR 运行中将时间推进插值耗时降低约 11.0%，总计算时间降低约
2.6%。两段运行的后期网格统计略有差异，因此这是受控程度较高的端到端结果，仍不等同于
固定网格 kernel 基准。旧的 `interp_fillpatch` 同时累计时间推进和 `RemakeLevel()` 的
regrid 填充，可能略大于 `interp`；新增 `interp_regrid_fill` 和
`interp_regrid_fill_calls` 后，两种调用来源可单独解释。

增加归属字段后，job `575351` 再次运行相同 A/B。当前 direct 路径得到
`interp=29.4486 s`、`interp_fillpatch=29.7810 s`、
`interp_regrid_fill=0.3559 s`，即扣除 regrid 填充后为 `29.4251 s`，与外层
时间推进统计仅差约 `0.024 s`。历史 FPinfo 路径也满足同一关系。缓存构建仍为
`0.00463 s / 96 次`，证明布局构建已不再是插值热路径。
