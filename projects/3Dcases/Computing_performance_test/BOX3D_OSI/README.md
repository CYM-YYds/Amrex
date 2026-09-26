# BOX3D_OSI

`BOX3D_OSI` 是基于 AMReX 26.06、CUDA 和 MPI 的三维 D3Q27 LBM 动态 AMR
实验算例。相邻的 `BOX3D` 是数值基准；本目录同时保留双数组 A-B 路径和单数组
one-step index（OSI）路径。

## 当前验证状态

更新时间：2026-09-26。最新逐阶段/全 valid 矛盾与待测点见
[当前交接状态](docs/current_status.md)。

当前工作配置位于 `config/inputs`。每次提交时，脚本会把它冻结到独立运行目录，
因此后续修改当前配置不会改变任何历史运行记录。

- `amr.max_level=2`，即 level 0--2 三个 AMR level；
- `amr.regrid_int=32`；
- `amr.blocking_factor_x/y/z=32`；
- `lbm.stream_mode=1`，默认使用 OSI 单数组路径；A-B 基准通过显式覆盖为 0 启用；
- `lbm.collide_mode=1`，使用 D3Q27 专用碰撞核。
- OSI 所有地址路径统一使用预计算 phase shift，不再提供 legacy 取模开关；
- `performance.report_int=1000`，按窗口输出总阶段和逐 level 碰撞统计；
- 当前 `config/inputs` 为 `max_step=128000`、`amr.plot_int=3200`、
  `checkpoint.chk_int=32000`；OSI 通信配置为 host-staging、
  `osi_mpi_pipeline_chunk_bytes=2097152`，已随提交 `1acbea7` 固化。历史
  96000 步结果仍按历史作业单独引用；
- PlotFile 使用 `plt_` 前缀，checkpoint 使用 `chk` 前缀，均相对于本次运行目录。

长程历史作业 `logs/submit/586660-out.log` 运行到 step 96000，正常完成，未观察到
NaN、MPI abort 或异常终止。它只证明该历史源码与输入在该运行窗口内完成，
但**尚未收敛**：

```text
CONVERGENCE step=96000 interval=3200
velocity_l2_relative=5.530030118e-4
tolerance=1e-12 consecutive=0/3 converged=0
```

这也不是 BOX3D 与 BOX3D_OSI 的逐网格 A-B 等价证明。双网格正确性验收仍须在相同
输入、BoxArray、MPI/GPU 配置和 regrid 时序下，从第一步开始逐 level、逐阶段、逐 cell
比较 active DDF 和宏观量。`stream_mode=1` 不能用于解释 `stream_mode=0` 的差异。

最新同层跨 MPI 对照使用 2 ranks、2 GPUs、单层 8 Fab 和全周期边界。
2 MiB host-staging 分块流水在 job `596145` 的 384 次六阶段检查中全部
`linf=0`；job `596146` 中将 OSI communication 从 3.848--3.854 s 降至
3.518--3.529 s，total 从 4.381--4.386 s 降至 4.045--4.049 s，比同作业
FillBoundary 的 4.616--4.622 s 快约 12.3%--12.5%。六面非周期 job `596150`
亦完成 384/384 次 `linf=0`。该结论仅覆盖单节点、单 peer、单层同层通信；
该历史作业的 `lbm.osi_mpi_pipeline_chunk_bytes` 为 0，已测最佳 2 MiB 需显式设为
`2097152`。CUDA-aware device-buffer 路径已实现并有能力门禁，但当前 HMPI/UCX
不支持 device pointer；`591185` 在强制开启时以 `process_vm_readv: Bad address` 失败。
改用平台 CUDA-aware OpenMPI 4.1.5 与配套 UCX 后，job `595584` 的 device-buffer
64 步六阶段 A-B 全部 `linf=0`。但 job `595585` 中 device-overlap 为约 45.90 s，
而 host-overlap 为约 4.13 s；约 44.85 s 消耗在 MPI wait，因此当前 CUDA-aware
transport 只通过正确性验收，不适合作为性能路径。
该结论只覆盖同层通信，不能外推到多层动态 AMR。完整统计和适用边界见
[性能分析](docs/performance_profiling.md)。

当前源码会在完整 `AverageDownValid()` 后对当前 DDF 重新施加非平衡外推边界：A-B
修复 `f_old`，OSI 修复当前 phase 的 `osi_state`，并包含 covered 物理边界单元。该操作
用于避免这些单元在重网格后重新暴露或参与插值时仍保留被平均后的边界值。step 32
逐点检查确认 step 32 入口、调用它之后、重网格之后的 level 0 全 valid
DDF 都与 A-B 完全一致。此前 `0.07579002442` 来自不可靠的旧范数诊断；
详见交接状态。

完整状态和证据边界见 [当前交接状态](docs/current_status.md)。

## 构建与运行

在本算例目录执行：

```bash
./scripts/compile.sh
dsub -s ./scripts/submit.sh
```

通过 `scripts/submit.sh` 提交的作业会创建 `runs/<timestamp>_job<job-id>/`，并在其中保存只读
`inputs` 快照、`overrides.txt`、`command.txt`、`manifest.txt`、`run.log`、PlotFile
和 checkpoint。程序从该目录启动，因此所有相对输出路径都与本次输入绑定。

切换算例时直接编辑 `config/inputs`。需要复用历史配置时，可先把某个
`runs/<run-id>/inputs` 复制回 `config/inputs`，修改后再提交；旧运行目录不得回写。

正式生产计算建议不使用 `AMREX_RUN_ARGS` 临时改变物理或网格参数；确需覆盖时，脚本
会把原始字符串写入 `overrides.txt`，但它仍不如独立算例定义直观。

现有 `scripts/submit_*.sh` 是专项验证入口，各自管理工作目录和输出，不自动采用上述
`runs/` 快照流程。从历史 checkpoint 续跑时，通过 `RESTART_CHECKPOINT` 指定完整的
checkpoint 目录；主提交脚本会在新运行目录创建本地 `chk<step>` 只读符号链接，并自动
覆盖 `checkpoint.begin_step` 和 `checkpoint.chk_prefix=chk`。后续 checkpoint 仍写在新
运行目录，不会写回或清理历史源目录。

构建选项以 `config/GNUmakefile` 为准；当前使用 AMReX 26.06、MPI、CUDA 和
TinyProfiler。集群覆盖参数应通过 `AMREX_RUN_ARGS` 传给提交脚本。专项复现入口位于
`scripts/submit_*.sh`，对应日志检查位于 `tests/`。

## 运行时网格参数

`config/inputs` 是网格和物理域的运行时入口：

- `amr.n_cell` 设置 level-0 的 `NX/NY/NZ`；
- `geometry.prob_lo`、`geometry.prob_hi` 设置物理域边界；
- `geometry.is_periodic` 设置三个方向的周期性。

程序启动时会校验尺寸、边界和各向同性 cell spacing，并把派生的 `dx/dt`、最细层
`dx_min/dt_min` 放入轻量 `LbmGridParams`，显式传给 AMR、粒子和 GPU kernel。当前
碰撞模型要求三个方向的粗网格 spacing 相同，因此修改 `n_cell` 时应同步调整域长度。
`D3Q27` 速度集、`Q`、最大编译层数和物体直径 `D` 仍属于编译期模型参数。

## 模式和验证边界

- `stream_mode=0`：双数组数值基准路径，供正确性和性能 A-B 使用。
- `stream_mode=1`：grown-Fab OSI 单数组路径；已有单层、两层、静态/动态 AMR 和
  canonical checkpoint 的历史测试，但不能外推为当前多层生产验收。
- `lbm.osi_parallel_copy=1`：启用插值和平均阶段的 OSI direct 路径；多 rank
  平均通过 CPC tags 将 canonical restriction 结果打包后直接写入 coarse OSI raw。
  当前配置设为 `1`。同层 MPI direct 和 canonical fallback 已分别在 jobs
  `603875`/`603876` 的 2-rank、64-step lockstep 中通过阶段逐值检查；device-direct
  仍未在当前 HMPI/UCX 环境运行验收。
- 静态 `ParticleContainer` 的 checkpoint/restart 已有跨 MPI 分解测试；运动刚体的
  质心、平动/角速度、力和力矩未持久化，尚不支持完整 IBM restart 结论。
- 历史性能 job 和图片只代表当时的源码、输入和硬件，不是当前性能基线。

## 文档导航

- [当前交接状态](docs/current_status.md)：现役配置、最新运行结论和未决事项。
- [OSI 算法与架构](docs/osi_algorithm_and_architecture.md)：地址映射、phase、通信、
  AMR 传输和 checkpoint 设计。
- [OSI 实施与验证计划](docs/osi_implementation_plan.md)：历史阶段范围和验收记录。
- [OSI 跨 MPI 通信计划](docs/osi_mpi_communication_plan.md)：OSI-aware pack/unpack
  的实现阶段、正确性矩阵和性能验收标准。
- [DDF 粗细网格填充](docs/DDF粗细网格填充学习文档.md)：粗细层数据语义。
- [性能分析](docs/performance_profiling.md) 与 [MLUPS 记录](docs/MLUPS记录.md)：
  历史性能证据及其适用边界。
