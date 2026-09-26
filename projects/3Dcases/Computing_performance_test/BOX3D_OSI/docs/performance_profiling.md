# BOX3D_OSI 性能分析与历史 A-B 基线

## 2026-09-14：最新受控通信对照：host-staging 分块流水

当前最新受控对照为 job `596146`：2 ranks、2 GPUs、单层 8 Fab、全周期、
1000 步，同一资源分配内依次运行 FillBoundary、整块 OSI host-staging 和
2 MiB chunk 流水。

| 路径 | rank 0 total (s) | rank 1 total (s) | rank 0 comm (s) | rank 1 comm (s) |
| --- | ---: | ---: | ---: | ---: |
| FillBoundary | 4.62224 | 4.61639 | 3.72053 | 3.71359 |
| OSI 整块 host-staging | 4.38567 | 4.38076 | 3.85391 | 3.84765 |
| OSI 2 MiB 分块流水 | 4.04451 | 4.04935 | 3.51753 | 3.52852 |

2 MiB 流水相比整块 OSI 将 communication 降低约 8.3%--8.7%，total 降低
约 7.6%--7.8%；相比同作业 FillBoundary 的 total 快约 12.3%--12.5%。
jobs `596147`/`596148`/`596149` 的 1/4/8 MiB 扫描中，2 MiB 为已测最佳。
周期 job `596145` 和六面非周期 job `596150` 均完成 384/384 次六阶段
`linf=0`。该结论仅覆盖单节点、单 peer、单层同层通信；这些历史作业的运行参数
`lbm.osi_mpi_pipeline_chunk_bytes` 为 0。当前 `config/inputs` 已设为 2 MiB，
但不能将该值自动外推到多节点或多层 AMR；当前配置的 2-rank 生产和 lockstep 复测见
jobs `603873`、`603875`。

## 2026-09-14 CUDA-aware MPI 验收

使用 CUDA-aware OpenMPI 4.1.5、UCX 1.12.1、2 ranks、2 GPUs、单层 8 Fab、全周期
边界。job `595584` 的 64 步六阶段逐点 A-B 共 384 项全部 `linf=0`，证明 device-buffer
通信在该范围数值正确。job `595585` 的 1000 步同作业对照如下：

| 指标 | canonical A-B | OSI host overlap | OSI device overlap |
|---|---:|---:|---:|
| solver | 45.519--45.535 s | 4.128--4.140 s | 45.896--45.908 s |
| `MLUPS_solv` | 46.06--46.07 | 506.56--508.09 | 45.68--45.69 |
| communication | 44.470--44.496 s | 3.596--3.610 s | 45.145--45.156 s |
| OSI MPI pack | 不适用 | 0.775--1.078 s | 0.201--0.202 s |
| OSI MPI wait | 不适用 | 1.767--2.055 s | 44.846--44.860 s |
| OSI MPI unpack | 不适用 | 0.762--0.764 s | 0.091--0.095 s |

device-buffer 消除了 host staging，pack/unpack 明显缩短，但 MPI wait 增至约 44.85 s，
使其比 host-overlap 慢约 11.1 倍。同一 OpenMPI/UCX 栈上的 canonical `FillBoundary`
也出现约 44.5 s 通信耗时，因此瓶颈属于当前 CUDA-aware MPI transport，而不是 OSI
地址映射。该结果不能与 HMPI job `591187` 的约 5 s A-B 基线混为同一软件栈比较。

## 2026-09-12 当前双 GPU 同层 MPI 基线

job `591181` 在同一作业内使用 2 ranks、2 GPUs、单层 8 Fab、全周期边界和 1000 步，
依次运行当前二进制的 A-B 与 OSI MPI direct：

| 指标 | A-B | OSI MPI direct | OSI / A-B |
|---|---:|---:|---:|
| solver | 4.980--4.988 s | 5.504--5.517 s | 1.106--1.108x |
| `MLUPS_solv` | 420.40--421.09 | 380.12--380.99 | 0.903--0.906x |
| communication | 4.072--4.091 s | 4.968--4.989 s | 1.214--1.225x |

OSI MPI direct 仍慢约 10.8%。上一实现的同作业 `591174` 慢约 31.7%；通信计划和
staging 缓冲缓存化、pack/unpack kernel 融合把差距缩小约三分之二。job `591180`
使用相同双 GPU 拓扑完成 64 步六阶段逐点 A-B，全部 `linf=0`。该性能结论只覆盖单层
同层通信；host staging 和 MPI wait 仍是下一阶段热点。

## 2026-09-12 seam/MPI 重叠结果

job `591187` 保持 `591181` 的 2 ranks、2 GPUs、单层 8 Fab、全周期和 1000 步配置，
但在投递远端 MPI 后启动本地 seam copy，使其与 MPI wait 重叠：

| 指标 | A-B | OSI host overlap | OSI / A-B |
|---|---:|---:|---:|
| solver | 5.027--5.035 s | 5.342--5.354 s | 1.061--1.065x |
| `MLUPS_solv` | 416.53--417.18 | 391.71--392.60 | 0.939--0.943x |
| communication | 4.128--4.133 s | 4.809--4.821 s | 1.164--1.168x |

job `591186` 在相同双 GPU 拓扑完成 64 步六阶段逐点 A-B，全部 `linf=0`。相较
`591181` 的约 10.8% 总耗时差距，本次降到约 6.1%--6.5%。

CUDA-aware device-buffer 直传已实现为显式实验路径，并使用
`ParallelDescriptor::UseGpuAwareMpi()` 做硬门禁。当前 HMPI/UCX 安装不提供可用的
CUDA transport；job `591185` 强制声明支持后，UCX 对 device pointer 调用
`process_vm_readv` 并报 `Bad address`。因此当前没有 device 直传性能数字，也不能把
强制参数当成能力证明。

## 2026-09-11 地址与边界诊断结果

当前 if/else 版本 `589647` 与 branchless 试验 `589649` 的 1000 步生产结果几乎相同：
Collision 为 17.4206/17.4217 s，Communication 为 9.2777/9.2739 s，Boundary 为
2.4880/2.4690 s，solver 为 34.2746/34.2732 s。solver 仅改善约 0.004%，而 branchless
使相关 kernel 寄存器从 34 增至 36，因此已恢复 if/else。严格阶段 oracle `589641`
和 `589648` 均在单层非周期 64 步窗口通过全部阶段，说明回退不影响数值结果。

Boundary 坐标缓存对照 `589544`→`589593` 将 Boundary 从 2.7042 s 降到 2.4847 s
（约 8.1%），但 solver 只变化约 0.07%；这是局部 kernel 收益，不是端到端加速。

## OSI 预计算地址实验

jobs `589418` 与 `589419` 在同一算例、同一 1000 步工作量上分别测试
legacy 动态取模和预计算位移。当前源码只保留后者，并已扩展到所有
OSI 读写、边界、通信、插值、限制、诊断和检查点地址路径。

| 指标 | legacy `589418` | optimized `589419` | optimized / legacy |
|---|---:|---:|---:|
| Collision | 54.6415 s | 18.3701 s | 0.336x |
| Solver | 93.2050 s | 57.6280 s | 0.618x |
| `MLUPS_solv` | 407.13 | 658.48 | 1.617x |
| level 0 Collision | 2.7085 s | 0.9165 s | 0.338x |
| level 1 Collision | 6.7338 s | 2.3399 s | 0.347x |
| level 2 Collision | 45.1992 s | 15.1138 s | 0.334x |

该结果表明原 OSI Collision 的主要额外成本来自每个 cell、每个方向的整数取模，且
不是某个 AMR level 独有。job `589420` 的单层非周期 64 步逐步 A-B 比较全部
`linf=0`，证明这一地址替换在该范围内保持数值结果；job `589421` 在动态 regrid 后的
step 32 触发 A-B 断言，因此尚不能外推到多层动态 AMR。旧 job `589457` 已被后续
`589647`/`589649` 诊断取代，不再作为当前基线。

以下章节是继承的 BOX3D A-B 历史资料；其中 job、图表和性能数字不能作为当前源码的
直接基线。

## 目的

复制基线用于测量递归 `JaberCycle2()` 路径中的粗细网格 AMR 传输开销。相关的数据路径如下：

```text
FillGhostLevel -> FillDdfGhostFromCoarse
                 -> direct staging（正常时间推进）
                 -> FPinfo temporary patch（RemakeLevel 布局迁移）
AverageDownInterfaceLevel -> 固定的稀疏交界 restriction -> ParallelCopy
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

Jaber et al. 的 A6 cavity 测试与这个算例在大目标上相同：都是 `Re=1000`、`64^3`、D3Q27、双精度、四层 cavity 计算，并且每 32 个 coarse 步进行一次 regridding。两者都使用线性的 coarse-to-fine 空间插值。但它们并不是直接的性能对标对象：A6 是单 GPU、固定 `4^3` block、GPU 原生 octree 求解器，使用仅界面 restriction 和原地 shared-memory streaming。BOX3D 则使用 AMReX patch、通用 `FillPatchTwoLevels()`、coarse-level 的覆盖/界面 mask，以及双 MultiFab pull streaming。当前普通推进固定使用针对 LBM 的融合交界 restriction，并基于缓存的 coarse-interface parent 列表；在 regridding 之前仍会执行完整的 fine-valid restriction 和当前态物理边界修复。这些 mask 不是 Jaber 的 fine-level `cells_ID_mask`，而历史 job `571393` 也早于当前的仅界面路径。在比较 MLUPS 之前，应先匹配网格覆盖、马赫数、细化准则和 active-node 计数。

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

旧的统一 DDF 填充函数会对每个有效 coarse 单元都执行非平衡 DDF 重标定 kernel。当前正常推进缓存每个 fine ghost `work_box` 所需的精确 coarse stencil；`RemakeLevel()` 则根据 `FPinfo.ba_crse_patch` 建立临时 patch。两条路径都只缩放实际插值源区域，不再整层缩放。

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

以下模式编号仅描述 jobs `573417`/`573418` 所在的历史版本；当前源码已经删除
`lbm.average_mode`，普通推进固定使用稀疏交界融合 restriction，重网格前固定使用
`AverageDownValid()` 完整同步。

| 模式 | 区域 | 实现 |
|---:|---|---|
| 0 | 所有 fine valid cells | 使用 `f_new` 作为临时区的拆分 copy/scale/restrict |
| 1 | coarse-fine interface | 融合 sparse scale/restrict |

模式 1 使用缓存的 sparse coarse-parent Box 列表。该列表会在 regridding 后重建，并且必须与 coarse `interface_mask` 具有完全相同的单元数量。正常时间步中的 restriction 只更新这个 collar；而在每次 regrid 前的现有 `AverageDownValid()` 调用，会在覆盖的 coarse 单元暴露之前执行所需的完整同步。

下面的 jobs `573417`/`573418` 仍作为历史优化记录；其中模式编号不再对应当前配置项。

jobs `573417`（模式 2）和 `573418`（模式 3）都是单 GPU、1000 步运行。两者处理了 274,898,288 个 coarse parents 和 2,199,186,304 个 fine children。它们的 31 组 regrid mesh-statistics 序列完全一致。

| 数量 | 模式 2 | 模式 3 | 变化 |
|---|---:|---:|---:|
| Average total | 22.4954 s | 5.7458 s | -74.46% |
| `JaberCycle2` | 237.9553 s | 226.1978 s | -4.94% |
| Compute total | 239.9378 s | 228.3266 s | -4.84% |
| `MLUPS_total` | 269.75 | 283.47 | +5.09% |

模式 2 用 4.5648 s 将界面子单元拷贝到 `f_new`，用 3.5150 s 对它们进行缩放，用 13.6801 s 做 restriction，并用 0.7121 s 将结果拷回。当时的模式 3 用 4.7930 s 跑融合 kernel，并用 0.9360 s 拷回结果。Average 的大幅下降使该路径成为默认值，它现已重编号为模式 1。

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
  -> 按 lbm.interp_mode 直接写入 fine ghost Box
  -> fine FillBoundary 和物理边界填充
```

缓存的几何信息为每个 coarse staging Box 包含一个物理边界标志。这样可以在后续时间步中去掉重复的主机端 Box/domain 检查，但不会去掉占主导的 DDF 数据搬运或 kernel（`ParallelCopy`、`average_scale`、插值和 fine `FillBoundary`）。

## 三种粗到细插值模式：Job 580487

`scripts/submit_interp_ab.sh` 从同一个 `chk00009000` 重启，固定 level 0--3 网格并在同一
GPU 节点依次运行 mode 0、1、2，各统计 1000 步。三组的 `fillghost_calls=7000`，网格、
调用次数和其他工作计数完全一致：

| 指标 | 三线性 0 | 守恒线性 1 | 单元二次 2 |
| --- | ---: | ---: | ---: |
| `interp` | 113.3439 s | 142.0894 s | 126.6074 s |
| `interp_fillpatch` | 113.3352 s | 142.0810 s | 126.5988 s |
| 总耗时 | 236.5582 s | 265.3746 s | 249.7678 s |
| `MLUPS_total` | 95.7778 | 85.3775 | 90.7123 |

相对三线性，守恒线性插值耗时增加 25.36%，二次插值增加 11.70%；二次插值本身比守恒
线性快 10.90%。守恒线性的受限斜率包含数据相关条件、`abs/min/copysign`，而二次公式
虽读取更多 stencil 点，却主要由可融合的规则乘加组成。

该 checkpoint 的绝对 MLUPS 不代表生产布局：level 1/2/3 分别有 352/284/499 个 Box，
多数 Box 只有 `16^3` cells，GPU launch、ghost 和通信固定成本占比很高。固定 checkpoint
只用于保证三模式工作量相同；正常动态 AMR 的 job 580468 中 mode 0/mode 2 分别报告
约 664.5/633.9 MLUPS，但两组网格演化略有分叉，不能作为纯 kernel 比较。

job 580469 在正常 regrid 下进行了 64 步 mode 0/mode 2 valid-DDF 比较，得到
`linf=1.6534e-4`、`rel_l2=4.8875e-5`，且无 NaN、CUDA 或 AMReX 错误。这证明当前测试
范围内执行稳定且两种结果接近；它不是解析解或网格收敛验证，不能据此声称二次插值的
物理误差更小。

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

初始建网、regrid 和 restart 后，`RebuildCoarseFineCaches()` 会建立 direct cache；
`FillDdfGhostFromCoarse()` 只消费已就绪的缓存。`RemakeLevel()` 使用 temporary patch，
不会构造或使用 direct cache。
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
