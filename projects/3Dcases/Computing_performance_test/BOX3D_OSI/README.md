# BOX3D_OSI：AMReX 动态 AMR 上的单数组 OSI-LBM 实验算例

`BOX3D_OSI` 以相邻的 `BOX3D` 为数值与性能基线，目标是在 AMReX patch-based
动态自适应网格上实现 one-step index（OSI）分布函数存储与隐式迁移。

## 当前状态

截至 2026-09-02，默认生产时间推进仍是从 `BOX3D` 复制得到的 A-B 双 `MultiFab`
pull-streaming 实现。OSI 阶段 1--4 的单层路径已经完成：`collide_mode=1` 下支持
多 Fab、多 MPI rank，以及三向全周期或六面非周期边界。当前采用
`nGrow=2 + grown-Fab OSI`：
每个 Fab 的 valid 与 ghost 组成统一保护环，通信同步当前 phase 的同坐标重叠副本。
非周期边界在 phase 提交后直接从迁移后的 `osi_state` 重建并写回边界槽位。阶段 5 已接入
两层 AMR 子循环和 OSI-aware 粗细层传输。阶段 6 已实现不经过 `f_old/f_new` 的 direct
OSI regrid：旧/新 fine 重叠区按旧 phase 分批解码并 remap，新增 fine patch 从 coarse
OSI 状态插值，改变布局的 level 重置到 phase 0。静态与动态路径均已完成双 MPI rank
A-B checksum 回归。阶段 7 已接入 canonical checkpoint/restart 和宏观量 plotfile：
checkpoint 不持久化 raw twisted 地址，restart 统一恢复为 phase 0 并重建通信及粗细缓存。
静态 `ParticleContainer` 的 AoS/SoA 已纳入 1-rank 写出、2-rank 重启验收；
运动刚体 host 状态和完整 IBM 耦合仍未验收。
现有性能证据也只覆盖单层全周期固定网格，不能外推到动态 AMR 生产负载。

复制完成后的审查基线提交为 `6578093`；开始修改代码前的文档基线提交为
`7a929e9`；阶段 1 地址实现及测试由提交 `b0e80b9` 固定。这些提交的说明均为
`before codex`。

当前源码保留的 A-B 基线路径具有以下特征：

- `f_old[lev]` 和 `f_new[lev]` 各保存一套 D3Q27 分布函数；
- `Collide()` 原位更新 `f_old`；
- `CommunicateLevel()` 填充 `f_old` 的同层 ghost；
- `Stream()` 从 `f_old(i-e_q)` 显式 pull 到 `f_new(i)`；
- `Boundary()` 修正迁移后的物理边界，随后 `SwapLevel()` 交换两套 `MultiFab`；
- `JaberCycle2()` 对细层做 2:1 时间子循环，并在运行中执行动态 regrid。

不要把继承自 BOX3D 的历史 job、图表或性能数字描述为 OSI 结果。

已完成的 OSI 阶段 1--4 内容：

- `src/OsiIndex.H` 提供 host/device 共用的 `positive_mod()`、`osi_coord()` 和
  `osi_address()`；
- 映射只使用单个 Fab 的 `lo/length`，`phase` 在乘法前对各轴长度取模；
- `tests/osi_index_test.cpp` 覆盖 D3Q27 全方向、非零 `smallEnd()`、三轴不同长度、
  超大 phase、排列性和 `A_q(x,p+1)=A_q(x-e_q,p)`；
- CPU 地址测试和完整 MPI+CUDA 构建已通过；阶段 2 production kernel 已调用该 helper。
- `lbm.stream_mode=1` 的正式路径只分配一份 `nGrow=2` 的 `osi_state` 和
  per-level `osi_phase`，生产 CUDA collision 通过 `osi_address()` 原位访问 D3Q27；
- `osi_sync_buffer` 按 `lbm.osi_sync_batch_components` 分批复用：从当前 phase 解码
  `FillBoundary()` 实际需要的 post-collision valid 源区，同步同逻辑坐标 ghost，再把
  实际目标 ghost 编码回当前 phase；通信区域由独立几何缓存确定，不读取 AMReX 私有
  `CopyComTags`；
- 每层布局建立时缓存互不重叠的 Decode/Encode Box，并用 `TagVector` 把一个 rank 上的
  小 Box 融合为每批各一次 GPU launch，避免稀疏裁剪增加 kernel 启动开销；
  随后通过 phase 提交完成 streaming，不跨 Fab 写下一 phase 目标；
- `verification.osi_ab_check=true` 才会额外分配并推进 A-B reference，每步比较全部
  valid DDF，容差为 `1e-12`；默认 false 时不分配 `f_old/f_new`；
- job `581325` 完成 32 个 GPU 步，逐步 `linf` 为 `0` 或
  `5.551115123e-17`。这是受限路径的数值等价证据，不是通用 OSI 或性能结果。
- `ComputeMacroLevel()` 通过 OSI accessor 直接读取 twisted state，然后只对普通宏观
  `MultiFab` 执行 `FillBoundary()`；源码中不再保留完整 `osi_canonical`；
- `config/inputs_osi` 把 `64^3` 域切成 64 个 `16^3` Fab，并以确定性非均匀 DDF
  初值避免均匀场掩盖通信错误；
- grown-Fab jobs `582016`（1 rank）和 `582017`（2 ranks）均完成 32 步，最大
  `linf=1.498801083e-15`，逐步误差序列完全一致。64 个 `16^3` Fab 下，一份 grown
  DDF 为 13,824,000 个值，复用同步缓冲为 512,000 个值。CPU A/B 还验证了初始同步
  后连续两步不刷新 ghost 的保护区不变量。验证模式仍含 A-B oracle，因此不是性能证据。
- 阶段 4 只遍历物理边界 valid cells；提交 phase 后，边界重建从新 phase 的内部
  `osi_state` 读取并直接写回同一 phase 的边界槽位，不再分配 boundary scratch。
  jobs `583246`/`583247` 在六面非周期、64 Fab、1/2 rank 下完成 32 步逐步 A-B，
  最大 `linf=1.443289932e-15`，误差序列完全一致。
- 阶段 5 已支持 `amr.max_level=1` 的两层递归推进：粗层一步、细层两个半步，
  并在 regrid 后重建 OSI 状态与粗细层缓存。粗到细使用分批解码的 sparse
  coarse staging 和直接 OSI 写入，细到粗使用 interface-only 融合 restriction。
  早期 jobs `584122`/`584123` 完成 4 步；严格静态 jobs `584361`/`584362`
  （1 rank）和 `584363`/`584364`（2 ranks）均以 `amr.regrid_int=-1` 完成 32 个
  coarse steps。level 0/1 每步全部 D3Q27 active checksum 在 OSI 与 A-B 间逐项一致。
  两 rank 作业覆盖跨 rank Fab 通信，但共享一块 GPU；这些 checksum 仍不替代
  multi-GPU/multi-node 或性能验证。
- jobs `584471`（OSI）和 `584472`（A-B）进一步验证静态两层无 `f_old/f_new`
  常驻中转：64 条 level 0/1 checksum 的 `active_sum` 与全部 `active_q0...26`
  完全一致；Arena 峰值分别约 204 MB 与 303--310 MB。该内存数字来自不同执行路径，
  只作为分配模式证据，不是受控性能结论。
- 阶段 6 曾用现已删除的确定性动态打标测试钩子，每两步依次触发布局改变、删除细层、
  重新创建细层和再次布局改变。jobs `584483`（OSI）与 `584484`（A-B）以
  2 MPI ranks 完成 10 个 coarse steps；日志中的 finest level 为 `1 -> 0 -> 1`，共
  18 条 `(step,level)` 记录的 `active_sum` 和 `active_q0...active_q26` 逐字符一致。
  OSI 日志保持 `full_ddf_arrays=1`，Arena 峰值约 278--309 MB；该动态峰值包含 regrid
  期间的稀疏 patch staging，不代表常驻第二套完整 DDF。
- 阶段 7 job `584620` 使用真实涡量 `ErrorEst` 判据生成两层全覆盖 AMR：1 rank 连续
  运行至第 16 步并写出 canonical checkpoint/plotfile，另一路在第 8 步 checkpoint 后
  改用 2 ranks 重启至第 16 步。level 0/1 的逐单元 D3Q27 比较均为 `Linf=0`，header
  明确记录 `LBMCheckpointV2` 和 `canonical_osi_single_array_v1`。该作业验证了静态两层、
  跨 MPI 分解 restart 与宏观量输出；动态 regrid 后 restart 和 multi-node 仍未覆盖。
  综合 job `584621` 还追加了 A-B V2 checkpoint 的 1-rank 写出、
  2-rank restart，对连续运行的全局 DDF 比较同样为 `Linf=0`。
- 增强 job `584839` 将 50,444 个静态拉格朗日点纳入同一套阶段 7 流程：
  OSI 在 1 rank 第 8 步写出 `ParticleContainer` AoS/SoA，然后以 2 ranks 重启至
  第 16 步。粒子数、坐标和 10 个属性的全局校验和在并行归约容差内一致，
  同时两层 D3Q27 全局 `Linf=0`。该验证只覆盖静态粒子容器的序列化；
  `centre` 、平动/角速度和力/力矩等 host 侧刚体状态尚未写入 checkpoint，
  因而运动粒子和完整 IBM 耦合仍未验收。
- job `584549` 在相同的 10-step、2-rank 动态 regrid 序列上，用 A-B 最终 checkpoint
  作为 canonical reference，对两个 level 的 D3Q27 逐单元比较。54 条 level/component
  记录全部通过：最大 active `Linf=1.054711873e-15`、最大 active mean-L1
  `=1.509245425e-16`、最大 active relative-L2 `=2.446320295e-15`。粗层 full-valid
  `Linf=1.61384318e-3` 来自被细层完全覆盖且按设计不再演化的 coarse cells；按求解所有权
  排除这些 deep-covered cells 后，level 0 active `Linf=8.881784197e-16`。
- 新建 fine level 的 A-B 与 OSI 路径统一为“coarse 非平衡 DDF 按
  `tau_fine/(2*tau_coarse)` 缩放，再执行空间插值”。当前 OSI 在插值前显式刷新并复用
  常驻 `density/velocity` 和既有 DDF batch，不分配临时 4 分量宏观量容器，也不构造
  完整 canonical D3Q27。最终验证 job `584617` 还将 regrid 回调收敛为只初始化
  valid：`FillOsiFinePatchFromCoarse()` 始终构造 sparse fine patch；碰撞前不填 level 0
  同层 ghost，fine 的 `FillGhostLevel()` 只准备两步子循环需要的 coarse-fine ghost；
  所有同层/周期 ghost 均由 `OsiAdvanceLevel()` 在碰撞后、phase 提交前同步。
  该作业覆盖
  `Remake/Clear/MakeNew/Remake`，54 项逐单元验收全部通过，最大 active
  `Linf=1.054711873e-15`、mean-L1 `=1.492743922e-16`、relative-L2
  `=2.417721326e-15`。日志中的 OSI Arena 峰值仍为约 287--309 MB，说明本算例的
  全局峰值由其他阶段主导；本次结论是消除了该函数的临时分配，不能据此宣称全局峰值下降。

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
    -> 整个 grown Fab 同址读取/碰撞/写回
    -> 当前 phase 的实际通信 source valid 分批解码到同步缓冲
    -> FillBoundary 同步同坐标 grown ghost
    -> 实际通信 destination ghost 编码回当前 phase
    -> 新 phase 物理边界重建
    -> phase 前进，隐式完成内部 streaming

需要 AMReX 按逻辑坐标操作时：
    twisted MultiFab
    -> 只对所需 region/分量做临时 gather
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

非周期物理边界在 phase 提交后处理。标准非平衡外推从新 phase 下迁移后的
内部逻辑 cell 读取非平衡部分，并直接写入同一新 phase 的边界 OSI 槽位。
这条数据依赖不再需要 boundary scratch；六个面在单个 cell kernel 中仍按确定顺序
覆盖边和角。详细顺序见架构文档第 7 节。

采用规范化边界不是因为 OSI 与 AMR 数学上冲突，而是因为 AMReX 的
`FillBoundary()`、`ParallelCopy()`、插值、限制和 `VisMF` 默认不知道这层地址翻译。

## 第一版范围

第一版按风险从低到高推进：

1. 单 Fab、单层、固定网格、周期边界；
2. 多 Fab/多 MPI、单层固定网格；
3. 非周期物理边界；
4. 静态多层 AMR 与 2:1 子循环；
5. 动态 regrid 的 direct OSI remap/reset；
6. checkpoint/restart 与 canonical 输出；
7. OSI 原生通信及端到端内存/性能优化。

当前已完成阶段 6 的两层动态 AMR 双-rank 回归：OSI 正式路径在关闭 oracle 时不分配
`f_old/f_new`，但 A-B 实现仍
保留为可选择的数值基线；OSI 尚不能设为默认路径。运行时模式为：

```text
lbm.stream_mode = 0  # 现有 A-B 基线
lbm.stream_mode = 1  # OSI 实验路径
```

即使后续完成 MPI/AMR 回归，也应先保留可构建的双数组模式用于受控 A/B；是否最终移除
由完整回归与维护成本共同决定，不能仅凭单层周期结果决定。

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

所有 OSI smoke/performance 作业共用 `config/inputs_osi`；不同实验只在提交脚本中覆盖
少量参数。阶段 2 GPU smoke 由 `scripts/submit_osi_stage2_smoke.sh` 覆盖单 Fab 和 A-B
检查参数。当前 OSI 运行时断言允许最多两层 AMR，仍要求 `collide_mode=1`，
且在 output/restart adapter 完成前禁用 plotfile、checkpoint 和 restart。

阶段 3 使用同一份 64-Fab 输入分别验证单/双 rank：

```bash
dsub -s ./scripts/submit_osi_stage3_single.sh
dsub -s ./scripts/submit_osi_stage3_mpi.sh
./tests/check_osi_stage3_logs.sh \
  logs/submit/<single-job>-osi-stage3-single.log \
  logs/submit/<mpi-job>-osi-stage3-mpi.log \
  logs/submit/<single-array-job>-osi-stage3-single-array.log
```

关闭 A-B oracle、验证单体积数组分配的 smoke 直接使用 `config/inputs_osi` 和
`scripts/submit_osi_stage3_single_array.sh`。启动日志必须包含
`ab_check=0 full_ddf_arrays=1 ring_ngrow=2`，且不得出现 `osi_ab:`。job `582018` 已按
该配置完成 32 步，Arena 峰值 used 为 189 MB；同配置但启用两数组 oracle 的 job
`582016` 为 401 MB。这是分配模式证据，不是受控性能结论。

阶段 5/6 使用的确定性静态/动态打标参数及其专用提交、日志检查脚本已经删除。
上述 jobs 保留为历史数值证据，但当前 `ErrorEst()` 只使用真实涡量阈值。后续 AMR
回归需要基于物理判据重新设计可复现输入，不能继续调用已经移除的测试参数。

`lbm.osi_sync_batch_components` 默认为 1；允许范围为 1--27。jobs `582514`（1 rank）
和 `582515`（2 ranks）在同一作业内顺序比较 `B=1/3/9`：三种批大小的32步 A-B
最大 `linf` 均为 `1.498801083e-15`。无 oracle 的第二个1000步窗口中，`B=9` 相对
`B=1` 将单 rank `comm` 从 8.2607 s 降至 1.8910 s、双 rank最大 `comm` 从
7.0598 s 降至 2.3699 s；对应 Arena used 分别由 189/114 MB 增至 220/130 MB。
这是固定64³、64 Fab、同步计时路径的受控结果，不代表动态 AMR 或生产吞吐。

2026-08-25 完成通信区域裁剪：64 个 `16^3` Fab、`nGrow=2` 下，每批 Decode 从
262,144 cell 降到 151,552，Encode 从 512,000 降到 249,856，转换 cell 总数减少
48.15%。最初逐 Box launch 虽数值正确但性能退化，已由 rank-local `TagVector` 融合
替代。融合版 jobs `582526`/`582527` 的 1/2-rank 32 步序列一致，最大
`linf=1.498801083e-15`。jobs `582529`/`582531` 的 1/2-rank、1000 步第二窗口中，
`B=3` 的 `comm` 分别为 `0.8340/2.5462 s`；旧整区 jobs `582514`/`582515` 为
`2.8531/3.3909 s`，即这组历史同配置对照分别下降 70.8%/24.9%。它们不是同一作业内的
full-region/sparse-region 配对测试，因此仍需保留这个证据边界。

验证记录：2026-08-19，GCC 11.3 CPU 地址测试和 `MAKE_J=2 GEN_CCDB=0
./scripts/compile.sh` 的 MPI+CUDA 完整构建通过。默认并行度 16 的首次全量构建曾因
编译节点内存不足失败，因此后续构建使用并行度 2。2026-08-20，37 步独立 CPU A/B
测试通过；job `581325` 在上述阶段 2 约束内完成 32 步生产 GPU A/B 比较，最大
`linf=5.551115123e-17`。构建证据与数值证据必须分别引用，后者也不能外推到尚未实现的
物理边界或 AMR 路径。2026-08-21，canonical correctness jobs `581421`/`581420`
完成首轮 64-Fab 验证；直接稀疏通信 jobs `581430`/`581429` 随后通过。2026-08-24
按 AMR/IBM 方向改为 grown-Fab 重叠副本模型，jobs `582016`/`582017` 再次完成单/双
rank 32 步验证，逐步误差序列一致且最大 `linf=1.498801083e-15`；job `582018` 验证
关闭 oracle 时只有一份完整 DDF。

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
