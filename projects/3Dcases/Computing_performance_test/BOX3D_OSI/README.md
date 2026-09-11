# BOX3D_OSI

`BOX3D_OSI` 是基于 AMReX 26.06、CUDA 和 MPI 的三维 D3Q27 LBM 动态 AMR
实验算例。相邻的 `BOX3D` 是数值基准；本目录同时保留双数组 A-B 路径和单数组
one-step index（OSI）路径。

## 当前验证状态

更新时间：2026-09-11。

当前权威运行参数位于 `config/inputs`：

- `amr.max_level=2`，即 level 0--2 三个 AMR level；
- `amr.regrid_int=32`；
- `amr.blocking_factor_x/y/z=32`；
- `lbm.stream_mode=0`，默认使用 A-B 双数组数值基准；OSI 通过显式覆盖为 1 启用；
- `lbm.collide_mode=1`，使用 D3Q27 专用碰撞核。
- OSI 所有地址路径统一使用预计算 phase shift，不再提供 legacy 取模开关；
- `performance.report_int=1000`，按窗口输出总阶段和逐 level 碰撞统计；
- `max_step=1000`、`amr.plot_int=1000`，当前输入已切换为短程验证配置。

长程历史作业 `logs/submit/586660-out.log` 运行到 step 96000，正常完成，未观察到
NaN、MPI abort 或异常终止。它证明当前配置在该运行窗口内稳定，但**尚未收敛**：

```text
CONVERGENCE step=96000 interval=3200
velocity_l2_relative=5.530030118e-4
tolerance=1e-12 consecutive=0/3 converged=0
```

这也不是 BOX3D 与 BOX3D_OSI 的逐网格 A-B 等价证明。双网格正确性验收仍须在相同
输入、BoxArray、MPI/GPU 配置和 regrid 时序下，从第一步开始逐 level、逐阶段、逐 cell
比较 active DDF 和宏观量。`stream_mode=1` 不能用于解释 `stream_mode=0` 的差异。

当前 OSI 性能对照 `589418`/`589419` 使用相同 1000 步工作量：预计算地址路径把
Collision 从 `54.6415 s` 降到 `18.3701 s`，约为原来的 `0.336x`。job `589420`
完成单层非周期 64 步逐步 A-B 对照，全部 `linf=0`；job `589421` 在动态 regrid
创建 level 1 后的 step 32 触发 `linf > 1e-12` 断言，因此当前不能宣称多层动态 AMR
逐网格等价。完整统计和适用边界见 [性能分析](docs/performance_profiling.md)。

当前源码会在完整 `AverageDownValid()` 后对当前 DDF 重新施加非平衡外推边界：A-B
修复 `f_old`，OSI 修复当前 phase 的 `osi_state`，并包含 covered 物理边界单元。该操作
用于避免这些单元在重网格后重新暴露或参与插值时仍保留被平均后的边界值；目前仅完成
CUDA+MPI 构建验证，尚无受控数值对照结论。

完整状态和证据边界见 [当前交接状态](docs/current_status.md)。

## 构建与运行

在本算例目录执行：

```bash
./scripts/compile.sh
dsub -s ./scripts/submit.sh
```

构建选项以 `config/GNUmakefile` 为准；当前使用 AMReX 26.06、MPI、CUDA 和
TinyProfiler。集群覆盖参数应通过 `AMREX_RUN_ARGS` 传给提交脚本。专项复现入口位于
`scripts/submit_*.sh`，对应日志检查位于 `tests/`。

## 运行时网格参数

`config/inputs` 是网格和物理域的唯一运行时入口：

- `amr.n_cell` 设置 level-0 的 `NX/NY/NZ`；
- `geometry.prob_lo`、`geometry.prob_hi` 设置物理域边界；
- `geometry.is_periodic` 设置三个方向的周期性。

程序启动时会校验尺寸、边界和各向同性 cell spacing，并把派生的 `dx/dt`、最细层
`dx_min/dt_min` 放入轻量 `LbmGridParams`，显式传给 AMR、粒子和 GPU kernel。当前
碰撞模型要求三个方向的粗网格 spacing 相同，因此修改 `n_cell` 时应同步调整域长度。
`D3Q27` 速度集、`Q`、最大编译层数和物体直径 `D` 仍属于编译期模型参数。

## 模式和验证边界

- `stream_mode=0`：双数组数值基准路径，当前正确性工作的优先对象。
- `stream_mode=1`：grown-Fab OSI 单数组路径；已有单层、两层、静态/动态 AMR 和
  canonical checkpoint 的历史测试，但不能外推为当前多层生产验收。
- 静态 `ParticleContainer` 的 checkpoint/restart 已有跨 MPI 分解测试；运动刚体的
  质心、平动/角速度、力和力矩未持久化，尚不支持完整 IBM restart 结论。
- 历史性能 job 和图片只代表当时的源码、输入和硬件，不是当前性能基线。

## 文档导航

- [当前交接状态](docs/current_status.md)：现役配置、最新运行结论和未决事项。
- [OSI 算法与架构](docs/osi_algorithm_and_architecture.md)：地址映射、phase、通信、
  AMR 传输和 checkpoint 设计。
- [OSI 实施与验证计划](docs/osi_implementation_plan.md)：历史阶段范围和验收记录。
- [DDF 粗细网格填充](docs/DDF粗细网格填充学习文档.md)：粗细层数据语义。
- [性能分析](docs/performance_profiling.md) 与 [MLUPS 记录](docs/MLUPS记录.md)：
  历史性能证据及其适用边界。
