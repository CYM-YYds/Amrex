# BOX3D_OSI：AMReX 动态 AMR 上的单数组 OSI-LBM 实验算例

`BOX3D_OSI` 以相邻的 `BOX3D` 为数值与性能基线，目标是在 AMReX patch-based
动态自适应网格上实现 one-step index（OSI）分布函数存储与隐式迁移。

## 当前状态

截至 2026-08-20，默认生产时间推进仍是从 `BOX3D` 复制得到的 A-B 双 `MultiFab`
pull-streaming 实现。阶段 1 地址层和阶段 2 的受限 OSI 运行路径已经完成；阶段 2 只
允许单 MPI rank、单 level、单 Fab、三向周期边界和 `collide_mode=1`。多 Fab/MPI、
非周期边界、AMR、checkpoint 和性能路径仍未实现，不能把阶段 2 结果外推到这些范围。

复制完成后的审查基线提交为 `6578093`；开始修改代码前的文档基线提交为
`7a929e9`；阶段 1 地址实现及测试由提交 `b0e80b9` 固定。这些提交的说明均为
`before codex`。

当前源码仍具有以下基线特征：

- `f_old[lev]` 和 `f_new[lev]` 各保存一套 D3Q27 分布函数；
- `Collide()` 原位更新 `f_old`；
- `CommunicateLevel()` 填充 `f_old` 的同层 ghost；
- `Stream()` 从 `f_old(i-e_q)` 显式 pull 到 `f_new(i)`；
- `Boundary()` 修正迁移后的物理边界，随后 `SwapLevel()` 交换两套 `MultiFab`；
- `JaberCycle2()` 对细层做 2:1 时间子循环，并在运行中执行动态 regrid。

不要把继承自 BOX3D 的历史 job、图表或性能数字描述为 OSI 结果。

已完成的 OSI 阶段 1/2 内容：

- `src/OsiIndex.H` 提供 host/device 共用的 `positive_mod()`、`osi_coord()` 和
  `osi_address()`；
- 映射只使用单个 Fab 的 `lo/length`，`phase` 在乘法前对各轴长度取模；
- `tests/osi_index_test.cpp` 覆盖 D3Q27 全方向、非零 `smallEnd()`、三轴不同长度、
  超大 phase、排列性和 `A_q(x,p+1)=A_q(x-e_q,p)`；
- CPU 地址测试和完整 MPI+CUDA 构建已通过；阶段 2 production kernel 已调用该 helper。
- `lbm.stream_mode=1` 分配独立 `osi_state` 和 per-level `osi_phase`，生产 CUDA
  collision 通过 `osi_address()` 原位访问 D3Q27；
- 单 Fab 周期 ghost 按当前 phase 从 logical valid 映射到 logical ghost，然后只提交
  一次 phase，不调用显式 `Stream()`；
- 阶段 2 同时保留并推进 A-B reference，每步比较全部 valid DDF，容差为 `1e-12`；
- job `581325` 完成 32 个 GPU 步，逐步 `linf` 为 `0` 或
  `5.551115123e-17`。这是受限路径的数值等价证据，不是通用 OSI 或性能结果。

## 新会话的阅读顺序

计划实现或审查 OSI 时，按以下顺序阅读：

1. [OSI 算法与 AMReX 集成架构](docs/osi_algorithm_and_architecture.md)：权威设计、术语、状态不变量和 AMR 兼容策略；
2. [OSI 实施与验证计划](docs/osi_implementation_plan.md)：分阶段改动范围、接口草图和验收门槛；
3. `src/main.cpp`：当前 `JaberCycle2()` 的递归推进与 regrid 时机；
4. `src/OsiIndex.H` 与 `tests/osi_index_test.cpp`：已实现的 OSI 地址层及其不变量；
5. `src/AmrCoreLBM.H/.cpp`：DDF 所有权、通信、粗细层传输、重构和 checkpoint；
6. `src/Kernels.H`：当前 collision、pull-streaming 和 boundary kernel；
7. `config/inputs` 与 `config/GNUmakefile`：运行模式、AMReX 版本和构建设置；
8. 本 README 后面列出的 BOX3D 基线资料。

论文及教学原型位于仓库的
[`research/papers/OSI优化计划`](../../../../research/papers/OSI优化计划/)。其中：

- `A simple one-step index algorithm for implementation of lattice.pdf` 是 OSI 原论文；
- `A simple one-step index algorithm for implementation of lattice/full.md` 是本地解析文本；
- `learnosi.cpp`、`osi_step1.cpp`、`osi_step2.cpp`、`osi_step3.cpp` 是均匀网格参考原型，不是 AMReX 可直接移植实现。

## 已确定的总体方案

本算例采用“OSI 热路径 + AMR 布局边界规范化”的方案：

```text
普通固定布局时间步：
    OSI twisted storage
    -> 同址读取/碰撞/写回
    -> OSI-aware ghost/MPI
    -> 旧 phase 物理边界重建到 scratch
    -> phase 前进，隐式完成内部 streaming
    -> scratch 散布到新 phase 边界

需要 AMReX 按逻辑坐标操作时：
    twisted MultiFab
    -> gather/canonicalize
    -> FillPatch / average-down / regrid / checkpoint
    -> scatter 或直接建立新 OSI 状态
    -> phase 重置为 0
```

这里的 canonical layout 满足 AMReX 的普通约定：

```text
mf(i,j,k,q) == 物理 cell (i,j,k) 的第 q 个 DDF
```

OSI twisted layout 则满足：

```text
storage(Addr(fab,q,phase,i,j,k), q)
    == 物理 cell (i,j,k) 的第 q 个 DDF
```

非周期物理边界是原位 OSI 的一个特殊同步点。边界公式需要从旧 phase 的内部逻辑
cell 读取参考值，却要把重建结果写到新 phase 的边界地址；两者可能映射到同一个 raw
槽位。因此第一版不能一边读取旧 phase 一边直接原位写新 phase，而要先把完整边界
重建结果写入 boundary scratch，待全部旧 phase 读取完成后再提交 phase，并把 scratch
散布到新 phase。详细别名示例与边角覆盖规则见架构文档第 7 节。

采用规范化边界不是因为 OSI 与 AMR 数学上冲突，而是因为 AMReX 的
`FillBoundary()`、`ParallelCopy()`、插值、限制和 `VisMF` 默认不知道这层地址翻译。

## 第一版范围

第一版按风险从低到高推进：

1. 单 Fab、单层、固定网格、周期边界；
2. 多 Fab/多 MPI、单层固定网格；
3. 非周期物理边界；
4. 静态多层 AMR 与 2:1 子循环；
5. 动态 regrid 的 canonicalize/reset；
6. checkpoint/restart、完整回归与性能比较。

在前三阶段完成前，不删除 `f_new`，也不把 OSI 设为默认路径。建议保留运行时模式：

```text
lbm.stream_mode = 0  # 现有 A-B 基线
lbm.stream_mode = 1  # OSI 实验路径
```

只有在数值和 MPI/AMR 回归通过后，才能评估是否移除双数组基线。

## 构建环境

本算例继承 BOX3D 的构建环境：

- AMReX：`amrex-26.06`；
- C++：C++20；
- 编译器：GCC 11 或更新版本；
- 生产目标：MPI + CUDA。

在算例根目录构建：

```bash
./scripts/compile.sh
```

运行阶段 1 地址测试：

```bash
./tests/run_osi_index_test.sh
```

运行阶段 2 独立 CPU A/B 测试：

```bash
./tests/run_osi_stage2_test.sh
```

阶段 2 GPU smoke 使用 `config/inputs_osi_stage2` 和
`scripts/submit_osi_stage2_smoke.sh`。该输入会禁用 regrid、输出和 checkpoint，并由
运行时断言检查单 rank、单层、单 Fab和全周期约束。

验证记录：2026-08-19，GCC 11.3 CPU 地址测试和 `MAKE_J=2 GEN_CCDB=0
./scripts/compile.sh` 的 MPI+CUDA 完整构建通过。默认并行度 16 的首次全量构建曾因
编译节点内存不足失败，因此后续构建使用并行度 2。2026-08-20，37 步独立 CPU A/B
测试通过；job `581325` 在上述阶段 2 约束内完成 32 步生产 GPU A/B 比较，最大
`linf=5.551115123e-17`。构建证据与数值证据必须分别引用，后者也不能外推到尚未实现的
多 Fab、MPI、物理边界或 AMR 路径。

## 继承的 BOX3D 基线资料

以下文档和图表从 BOX3D 复制而来，用于理解现有 A-B/AMR 路径和建立后续 A/B
对照。除非文档明确记录新的 BOX3D_OSI job，否则其中数字只属于原 BOX3D：

- [DDF coarse-to-fine 填充基线](docs/DDF粗细网格填充学习文档.md)
- [AMR 网格通信基线](docs/amr_grid_communication.md)
- [BOX3D 历史性能资料](docs/performance_profiling.md)
- [BOX3D 历史 MLUPS 记录](docs/MLUPS记录.md)

## 证据等级

后续文档和提交必须区分：

1. 设计完成；
2. 代码已实现；
3. 静态检查通过；
4. 编译通过；
5. 作业完成；
6. 数值等价/守恒验证通过；
7. 受控性能 A/B 完成。

低等级证据不能替代高等级证据。例如“成功编译”不能写成“OSI 数值正确”，短程
smoke 也不能写成“性能提升”。
