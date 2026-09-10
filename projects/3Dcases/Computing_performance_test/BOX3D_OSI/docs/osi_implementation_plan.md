# BOX3D_OSI 实施与验证计划（历史记录）

> 本文保留阶段实施和历史验收证据，不再作为“当前状态”入口。现役配置、最新性能与
> 未决验证以 [当前交接状态](current_status.md) 为准。

本文把 [OSI 算法与 AMReX 集成架构](osi_algorithm_and_architecture.md) 转换为可执行的
开发步骤，并在每阶段末保留验收记录。只有满足本节验收条件后，才能将对应状态改为完成。

## 1. 当前基线

截至 2026-09-02，`BOX3D_OSI` 默认使用 OSI；A-B 基线仍可用
`lbm.stream_mode=0` 选择：

```text
f_old + f_new
Collide -> Communicate -> Stream -> Boundary -> Swap
```

本目录的复制状态由基线提交 `6578093` 固定；代码修改前的文档基线为 `7a929e9`；
阶段 1 地址实现及测试由提交 `b0e80b9` 固定。这些提交的说明均为 `before codex`。

阶段 1 已经完成：

- `src/OsiIndex.H` 实现 Fab-local host/device 地址 helper；
- `tests/osi_index_test.cpp` 和 `tests/run_osi_index_test.sh` 提供独立 CPU 测试；
- CPU 测试通过；MPI+CUDA 完整构建通过；
- 地址层本身的验收不构成 OSI 数值实现；阶段 2 已将该 helper 接入受限的生产 CUDA
  collision kernel。

阶段 2 已新增：

- `lbm.stream_mode=1` 的受限实验入口；
- 独立 `osi_state` 和 per-level `osi_phase`；
- OSI-aware BGK collision 和单 Fab周期 logical ghost 填充；
- CPU A/B 测试及生产 GPU 逐步 A/B valid-DDF 检查。

阶段 3 grown-Fab 通信路径已新增：

- 一份 `nGrow=2` 的 grown-Fab `osi_state`；
- 按可配置分量批大小复用的 canonical 同步 `MultiFab`；
- 独立缓存实际通信 source valid/destination ghost，按当前 phase Decode、
  `FillBoundary()` 同坐标同步并 Encode，且用 `TagVector` 融合小 Box launch；
- `verification.osi_ab_check` 条件化 A-B oracle；关闭时只分配一份完整 DDF；
- 非均匀 DDF 验证初值和单/双 rank 同序列日志检查。

阶段 4--7 还已完成非周期边界、静态两层子循环、direct OSI 动态 regrid，以及
canonical checkpoint/restart/宏观量 plotfile。当前仍没有：

- dynamic-regrid restart 与 multi-GPU/multi-node 回归；
- 非周期、AMR 和动态 regrid 条件下的受控性能结果。

四层扩展 job `585083` 在实际 level 0--3 的每个递归子阶段检查 D3Q27、密度和速度
有限性，但在第 128 个 coarse step 的动态 regrid 后捕获 NaN，未通过验收。分层 job
`585078` 中两层 active `Linf=4.996003611e-16`，三层中间层则为
`7.530773731e-5`。因此这份 OSI 验收结论仍只覆盖两层；三层以上 OSI 仅保留为显式
诊断路径。当前 `config/inputs` 已改为 level 0--2 的 `stream_mode=1` 性能诊断配置；
它仍不能作为多层 OSI 逐网格正确性已经通过的证据。
此前 A-B jobs `585096`、`585291` 属于 `MakeNewLevelFromCoarse()` 双重覆盖修复前的
结果。修复后的 job `585390`（`stream_mode=0,max_level=3,regrid_int=32`）在 step 32
确认 regrid 前、`ErrorEst(tag_cells=7942)`、`FillCoarsePatch()`、
`MakeNewLevelFromCoarse()` 和 `RefineMesh()` 返回后的 level 1 均与 BOX3D 一致
（`valid_sum=524278.3023`）。该早期定位已由后续分段复测取代；当前排查范围仍是
双数组推进子阶段，而不是首次建层或 `ErrorEst()`。
三层以上仍是诊断路径，不能据此宣称多层生产稳定性。

2026-09-04 已确认该分叉的布局根因是 OSI `AmrInfo` blocking factor `(2,2,2)` 与 BOX3D
基准 `(8,8,8)` 不一致，已在 `src/main.cpp` 修正。job `585731` 的四层
`stream_mode=0` smoke 显示 step 64 的 level 0--2 BoxArray、粗细覆盖掩码和 level 2
重构后 DDF checksum 均与 BOX3D 一致；这只证明对应短程窗口，不能作为长程稳定性结论。

后续同条件分段 jobs `585869`--`585879` 逐个覆盖 step 96、128、160、192 和 224：
两边一致通过 step 192，step 224 时 BOX3D 正常而 OSI 双数组路径失败。BOX3D job
`585891` 与 OSI job `585892` 使用对称的 `f_old/f_new` 阶段诊断；在 step 224 level 1，
`cycle2_after_fill_ghost`、`advance_after_collide` 和 `advance_after_communicate` 的逐 q
聚合 checksum 一致，首次可观察差异在 `advance_after_stream` 的 `f_new`。随后 level 2
在 `advance_after_boundary` 出现 active NaN。OSI 诊断从 tiled `tilebox()` 改为完整
`validbox()` 后差异仍然存在。下一步必须比较 Stream 的实际 launch box、逐 cell
`covered_mask` 和逐 cell/逐 q 的 `f_old`；聚合 checksum 相同不等价于输入逐点相同。

当前修复任务以 `BOX3D` 为双数组数值基准，仅处理 `lbm.stream_mode=0`。OSI
`stream_mode=1` 属于独立实现路径，不参与本轮 A-B 正确性判定。step 224 的作业
`585973`（BOX3D）和 `585974`（OSI）中，`STREAM_RANGE`/`STREAM_FAB` 显示
`growtilebox(n-1)`、covered cell 数、stream cell 数和每个 Fab Box 一致；但
`after_refine_mesh` 后 level 1/2 的 valid DDF 逐 q 范数一致，grown 范数不同，说明
Stream 输入的 ghost 数据已经分叉。因为该统计位于 `FillGhostLevel()` 与
`CommunicateLevel()` 之后，尚不能断言是插值函数本身；应在 `FillDdfGhostFromCoarse()`
和 `CommunicateLevel()` 各自返回后增加分层统计，再做逐 cell/逐 q `f_old` 对照。无 regrid
作业 `585966`/`585967` 也在 Stream 后分叉，因此动态网格重构不是必要触发条件。不要把
level 2 的 covered-inclusive Boundary NaN 误判为首次根因。

### uncovered 有限性口径与复测结论（2026-09-05）

物理 valid 统计只包含 `covered_mask == 0`；interface-covered/deep-covered 另行统计。
OSI job `586201` 从 BOX3D `chk00000223` 重启并完成 step 224–300：step 224、level 1
的 `Stream()` 在物理边界 uncovered cell 读取域外 ghost 时出现暂时非有限值，
`Boundary()` 后被清除，后续 `advance_after_boundary` 和 `advance_after_swap` 未见
uncovered NaN。因此旧的 300 步“发散”是将 covered 区域纳入 active 判定造成的误报；
这项结果不替代 BOX3D/OSI uncovered DDF 的逐点比较。BOX3D job `586199` 的重启
测试因独立 Segmentation fault 失败，不作为数值结论。

## 2. 总体开发策略

必须保留 A-B 基线，通过运行时参数做同代码树 A/B：

```text
lbm.stream_mode = 0  # A-B 数值基线
lbm.stream_mode = 1  # OSI，当前输入；多层动态 regrid 仍待逐网格验收
```

开发顺序遵循：

```text
地址数学
-> 单 Fab 固定网格
-> 多 Fab/MPI
-> 物理边界
-> 静态多层 AMR
-> 动态 regrid
-> checkpoint/restart
-> 性能优化
```

不要一开始同时改 collision 数学、通信裁剪、数据布局和 AMR 传输；每阶段只引入一个
新的错误来源。

## 3. 阶段 0：冻结和记录 A-B 基线

目标：建立后续 OSI 对照，不修改物理设置。

检查项：

- 固定 `config/inputs`、GPU、MPI rank 数和输出设置；
- 记录源码 commit、AMReX 版本、编译器和可执行文件名；
- 保存短程 checkpoint 和完整运行日志；
- 记录每层 BoxArray/DistributionMap 序列；
- 记录 valid DDF、宏观量和现有性能窗口。

验收：至少一次成功编译和一次短程 A-B 作业；数值 reference checkpoint 可被
`CompareDdfCheckpoint()` 正常读取。历史 BOX3D job 不能代替本目录的新基线。

## 4. 阶段 1：纯地址映射测试

状态：**已实现，并达到本阶段的代码、CPU 测试和 MPI+CUDA 编译验收；没有 LBM
运行证据。**

目标：在不接触物理 kernel 前证明 Fab-local OSI 映射正确。

实际新增的 device/host 共用 helper：

```cpp
positive_mod(value, length)
osi_coord(logical, velocity, phase, lo, length)
osi_address(logical_xyz, velocity_xyz, phase, fab_geometry)
```

`osi_address()` 接收 `e_q` 而不接收 `q`，使地址数学不依赖 D3Q27 的具体编号顺序；
返回值是可传给 AMReX `Array4` 的 raw 三维坐标，而不是跨 Fab 的全局线性地址。

必须测试：

1. `phase=0` 时映射为恒等；
2. D3Q27 的静止方向始终不移动；
3. 正负方向和面对角/体对角方向符号正确；
4. 对每个 `q,phase`，完整 Fab Box 映射是排列且无重复；
5. `A_q(x,p+1)=A_q(x-e_q,p)`；
6. phase 大于 Fab 长度、多个方向长度互不相同时仍正确；
7. 不依靠 `length * phase`，避免整数溢出；
8. 非零 `smallEnd()` 的 Fab 正确。

验收记录：`./tests/run_osi_index_test.sh` 通过；`MAKE_J=2 GEN_CCDB=0 ./scripts/compile.sh` 的 MPI+CUDA 完整构建通过。默认并行度 16 的首次全量构建因编译
节点内存不足失败，降低并行度后成功。此阶段不宣称 LBM 正确。

## 5. 阶段 2：单 Fab、单层固定网格 OSI

状态：**已完成本阶段核心推进的实现、CPU 测试、MPI+CUDA 构建和 GPU 数值 A/B
验收；OSI macro accessor 已在阶段 3 接入，canonical checkpoint/output 仍待实现。**

目标：隔离 AMR/MPI，只验证 OSI 隐式 streaming 与 A-B 数值等价。

建议暂时保留 `f_old/f_new`，新增单独 `osi_state[0]` 和 `osi_phase[0]`。不要复用
`f_new` 作为 OSI state，以免 A/B 状态互相污染。

### 5.1 kernel 框架

```cpp
for each logical valid cell x:
    for q in D3Q27:
        addr[q] = Addr(fab, q, phase, x)
        local_f[q] = osi_state(addr[q], q)

    rho, u = macro(local_f)
    post[q] = collide(local_f, rho, u)

    for q in D3Q27:
        osi_state(addr[q], q) = post[q]
```

先复用与基线相同的 collision 公式，不同时改变 `collide_mode`。

### 5.2 phase 提交

单 Fab 周期测试可在 collision 完成后：

```cpp
++osi_phase[0];
```

但正式接口应封装成 `AdvanceOsiLevel()`，避免调用者在失败或异步 kernel 未完成时提前
修改 phase。

验收：

- 小周期域逐 cell、逐 q 比较 A-B 与 OSI，第一步必须 bitwise 或接近机器精度；
- 多步 `linf/rel_l2` 在预定容差内；
- 质量和速度场不出现系统漂移；
- ComputeMacro 通过 OSI accessor 或 canonical view 得到正确结果。

实际验收记录：

- `tests/osi_stage2_ab_test.cpp` 在非零 Fab 起点、三轴不同长度和两层 ghost 的小周期域
  连续比较 37 步，`./tests/run_osi_stage2_test.sh` 通过；
- `MAKE_J=2 GEN_CCDB=0 ./scripts/compile.sh` 的 MPI+CUDA 构建通过；
- `scripts/submit_osi_stage2_smoke.sh` 基于 `config/inputs_osi` 覆盖单 Fab和 A-B 参数；
  运行时断言限制为单 rank、单 level、全周期、`collide_mode=1`，并禁止尚无
  canonical adapter 的 plot/checkpoint；
- job `581325` 成功完成 32 步。生产 CUDA OSI 和保留的 A-B reference 每步比较全部
  valid DDF，`linf` 为 `0` 或 `5.551115123e-17`；
- reference 推进和逐步 norm 是验证开销，因此该 job 不提供 OSI 性能证据。
- 阶段 2 当时仍由 `f_old` oracle 间接保证宏观量等价；阶段 3 已让
  `ComputeMacroLevel()` 通过 OSI accessor 直接读取 twisted state。plot/checkpoint
  仍由运行保护禁用，不能据此宣称 canonical 输出已接入。

## 6. 阶段 3：同层多 Fab 与 MPI

状态：**grown-Fab 重叠副本同步和独立通信区域缓存已实现，并通过单/双 rank 数值
验收；批大小有受控 A/B，full-region 与 sparse-region 尚无同一作业内配对 A/B。**

目标：让完整 grown Fab 使用统一 OSI 环，并按当前 phase 同步重叠逻辑副本。

通信接口应表达逻辑区域，不暴露发送端 raw 地址：

```text
Decode(owner valid x, q, phase, owner grown-fab)
canonical.FillBoundary / MPI
Encode(replica ghost x, q, phase, receiver grown-fab)
```

不能对 twisted state 直接调用普通 `FillBoundary()`。当前实现只对分批 canonical
缓冲调用它，并在通信前后分别使用发送/接收 Fab 的 grown geometry 做 phase-aware
decode/encode。通信前后逻辑坐标都是 `x`；`x -> x+e_q` 只由 phase 提交表达。

当前 Decode/Encode 不再扫描整个 valid/grown Fab。建层时根据 BoxArray、`nGrow` 和
周期平移计算通信源/目标区域，去重后缓存为 `[level][global Fab][Box]`。缓存只表达
逻辑区域，phase-aware raw 地址仍在每步 kernel 内计算。为了避免数百个小 Box 各自启动
kernel，本 rank 的缓存 Box 被持久 `TagVector` 融合；布局销毁时必须先释放 tags，再
释放它们引用的 MultiFab。

验收：

- 相同全局网格的 1-rank/2-rank/多-rank DDF 对比；
- 改变 Box 分解而不改变全局问题时结果一致；
- 专门检查跨面、跨边、跨角的 D3Q27 分量；
- MPI 结束后、phase 提交前，同逻辑坐标 ghost 副本与 owner valid 匹配。

实际验收记录：

- `config/inputs_osi` 将 `64^3` 域固定分解为 64 个 `16^3` Fab，并启用确定性
  非均匀 DDF 初值；
- job `582016` 使用 1 MPI rank，job `582017` 使用 2 MPI ranks；两者都完成 32 步，
  每步比较全部 valid-cell D3Q27 分量；
- 两个 job 的逐步 `linf` 序列完全一致，最大值为 `1.498801083e-15`；
- `ComputeMacroLevel()` 在 OSI 模式下通过 accessor 直接读取 twisted state，并在逐步
  A/B 验证中实际执行；该阶段当时尚未接入 checkpoint/restart，
  现行实现与验收见阶段 7；
- `tests/check_osi_stage3_logs.sh` 验证 rank/Box/seed 标记、32 步数量、`1e-12` 容差、
  正常 finalize 和单/双 rank 序列一致性；
- 验证路径包含 A-B oracle 和逐步 norm，不能用于性能结论；`osi_ab_check=false` 时不
  分配 `f_old/f_new`，启动日志报告 `full_ddf_arrays=1`。
- 单数组 job `582018` 完成 32 步且无 `osi_ab:` 输出；相同 1-rank/64-Fab 条件下，
  Arena 峰值 used 为 189 MB，而 A-B 验证 job `582016` 为 401 MB。
- 通信区域缓存把每批 Decode 从 262,144 cell 裁剪到 151,552，把 Encode 从 512,000
  裁剪到 249,856，总转换 cell 数减少 48.15%；jobs `582526`/`582527` 再次通过
  1/2-rank 32 步等价检查，最大 `linf=1.498801083e-15`。
- 逐 Box kernel 的试验 jobs `582523`/`582524` 虽数值通过但性能严重退化，因此没有
  保留该实现；最终使用 `TagVector` 做 rank-local 融合。jobs `582529`/`582531` 的
  1/2-rank、`B=3` 第二个 1000 步窗口报告 `comm=0.8340/2.5462 s`；旧整区 jobs
  `582514`/`582515` 为 `2.8531/3.3909 s`，同配置历史对照下降 70.8%/24.9%。这仍
  不能代替同一作业内的 full-region/sparse-region 配对 A/B。

阶段三的演化与当前落点：

1. 阶段 3.0：canonical 适配原型

   - 原型先验证任意 phase 下 Decode/Encode 后与 A-B valid DDF 一致；
   - 当前源码没有保留全量 `GatherOsiToCanonical`/`ScatterCanonicalToOsi` 接口，
     而是由 `CommunicateOsiLevel()` 分批处理实际通信区域；
   - 宏观量计算直接通过 OSI accessor 读取 twisted state。
2. 阶段 3.1：单 rank、多 Fab

   ```text
   twisted valid incoming DDF
       -> collide whole grown Fab
       -> gather canonical valid
       -> canonical.FillBoundary(periodicity)
       -> scatter logical ghost 到当前 phase
       -> phase += 1
   ```

   这条路径性能不是最终形态，但能首先验证多 Fab 逻辑是否正确。
3. 阶段 3.2：多 MPI rank

   - 相同 BoxArray 下比较 1 rank 与 2 rank。
   - 比较每步全部 valid DDF。
   - 专门检查跨面、跨边、跨角的 D3Q27 分量。
   - phase 必须是每层统一状态，并且只能在通信完成后提交。
4. 阶段 3.3：grown-Fab 重叠副本同步

   2026-08-24 取代了先前的 valid-only 稀疏迁移路径。64 个 `16^3` Fab、`nGrow=2`
   下，一份 grown DDF 为 13,824,000 个值；批大小 1 的同步缓冲为 512,000 个值。
   相对于两份 grown DDF 的 27,648,000 个值，DDF+同步缓冲理论存储减少约 48.1%。

不同 Fab 的 grown `length` 可以不同。MPI 交换的是当前 phase 下同一逻辑坐标的值，
接收端用自己的 grown geometry 重新编码；不传递 raw 地址，也不执行跨 Fab 的
`x,p -> x+e_q,p+1` 写入。

## 7. 阶段 4：非周期物理边界

状态：**单层六面非周期直接 state 路径已实现，并通过单/双 rank 32 步逐步
A-B；混合周期方向、长期物理验证和性能测试尚未完成。**

目标：使用标准非平衡外推，在 phase 提交后从迁移后的内部逻辑格点重建边界。

步骤：

1. 在旧 phase `p` 对 `grown Fab & physical domain` 碰撞，再同步同层 post-collision
   ghost；物理域外 ghost 不被当作流体格点；
2. 提交 `phase=p+1`，从新 phase 的内部逻辑值读取迁移后参考值；
3. 将每个边界格点的最终 Q 分量直接写入新 phase 的 `A_q(boundary,p+1)`；
5. boundary work boxes 仍是 logical Box，不缓存 raw OSI 地址。

边界重建必须在 phase 提交之后进行，并先读取源值再写入目标槽位，避免同一线程内的
原地地址别名。

当前 A-B 和 OSI 共用 compact 面选择语义：每个 cell 内按固定面顺序确定最终
边界参数，不使用多个面 kernel 并行覆盖同一边/角 cell。

验收：

- 单层 lid-driven cavity 与 A-B valid DDF/速度/密度对比；
- 六个面、十二条边、八个角分别有覆盖；
- 构造可发生跨 phase raw 槽别名的小网格测试，并通过竞争检测或确定性重复测试；
- 直接 state 结果与 A-B 的面、边、角最终覆盖优先级一致；
- boundary cache 经重新分块后仍只依赖 logical geometry；
- 不使用 `%` 地址循环冒充物理周期边界。

实际验收记录：

- `ApplyOsiBoundaryLevel()` 由已有 disjoint `boundary_work_boxes` 驱动，直接写入
  `osi_state` 的新 phase 边界槽位；
- 非周期碰撞 launch 限制为 `ring_box & domain`，同层通信仍在旧 phase 完成；
- jobs `583246`（1 rank）和 `583247`（2 ranks）在六面非周期、64 Fab 下完成 32 步，
  每步比较全部 valid D3Q27，最大 `linf=1.443289932e-15`，两组序列逐行一致；
- 回归 jobs `583248`/`583249` 重新通过全周期 1/2-rank 阶段 3 检查，最大
  `linf=1.498801083e-15`。

## 8. 阶段 5：静态多层 AMR 与子循环

目标：BoxArray 固定时支持 coarse/fine DDF 传输和每层独立 phase。

### 8.1 数据结构

在 `AmrCoreLBM.H` 中引入每层状态，而不是单个全局 step：

```cpp
amrex::Vector<std::uint64_t> osi_phase;
amrex::Vector<amrex::MultiFab> osi_state;
```

### 8.2 canonical adapters

首先实现 correctness 版本：

```cpp
GatherCanonicalValid(lev, dst);
GatherCanonicalRegion(lev, logical_box, dst);
ScatterCanonicalValid(lev, src, target_phase);
ScatterCanonicalGhost(lev, logical_box, src, current_phase);
```

函数名或参数必须显式说明 valid/ghost 范围。

### 8.3 coarse-to-fine

初版路径：

```text
gather coarse stencil
-> existing average_scale/interpolation
-> canonical fine ghost result
-> scatter 到 fine current phase
```

### 8.4 fine-to-coarse

初版路径：

```text
gather fine valid children
-> existing scale/restriction
-> canonical coarse result
-> scatter 到 coarse current phase
```

实际验收记录（更新至 2026-08-31）：

- 统一 `Cycle2()` 已按实际 `finest_level` 执行粗层一步、细层两个半步；
- `FillOsiGhostFromCoarse()` 分批解码粗层 valid 到同步缓冲，只向 sparse
  `coarse_stage` 复制插值 stencil，再由 OSI-aware kernel 直接写细层 ghost；
- `AverageDownOsiLevel()` 直接按细层 phase 读取 2x2x2 children，对 interface parent
  融合缩放与 restriction，分批送到粗层 owner 后编码回当前 phase；
- 早期 jobs `584122`/`584123` 完成 4 步；严格静态 jobs `584361`/`584362`
  （1 rank）和 `584363`/`584364`（2 ranks）在 `amr.regrid_int=-1` 下完成 32 个
  coarse steps，日志显示 `finest_level=1` 和 `average_mode=1`；
- level 0/1 每步全部 D3Q27 `active_sum` 与 `active_q0...active_q26` 在 OSI/A-B 间
  逐项一致。两 rank 作业覆盖跨 rank Fab 数据路径，但共享一块 GPU；
- 本记录证明静态两层生命周期、2:1 子循环和传输路径可运行并保持聚合 DDF 一致；
  随后的 job `584549` 已补充动态两层逐单元范数，multi-GPU/multi-node 与性能证明仍未完成。

验收：

- `osi_phase[lev]` 的增量与统一 `Cycle2()` 子循环次数一致；
- 固定 BoxArray 下 A-B/OSI 每层 valid DDF 在容差内；
- coarse-to-fine 使用 `interp_mode=0`，fine-to-coarse 使用当前固定的稀疏交界
  restriction 完成对照；
- coarse/fine 传输测试不依赖 raw twisted `ParallelCopy()`。

## 9. 阶段 6：动态 regrid

目标：在同步点执行 direct OSI remap/rebuild/reset。

状态：**已完成。动态回调不再读取或写入 `f_old/f_new`，也不构造整层 canonical
中转。** 当前调用链：

```text
AverageDownValid()
RepairCurrentStatePhysicalBoundary()
RefineMesh()
    -> RemakeLevel: old phase 分批解码重叠 valid，并插值新增 fine patch
    -> ClearLevel: 删除消失层的 OSI 数据与 tags
    -> MakeNewLevelFromCoarse: coarse phase 分批解码，按 tau 与 2:1 时间步缩放
                               非平衡 DDF，再初始化新 fine phase 0
    -> changed level phase = 0
    -> RebuildCoarseFineCaches()
推进层
    -> Cycle2 的 FillGhostLevel(fine): 只插值两步子循环需要的 coarse-fine ghost
    -> AdvanceOsiLevelImpl: 碰撞后、phase 提交前同步每层同层/周期 ghost
```

未改变布局的 level 保持原 phase；只有新建或 Remake 的 level 重置为 0。跨层操作始终
分别使用 coarse/fine 当前 phase，因此无需把所有 active levels 强制规范化。AMR 回调
只初始化 valid；ghost 不参与布局 remap。碰撞前只有 fine coarse-fine 保护区需要填充，
同层/周期 ghost 在碰撞后由通信入口统一管理。

验收：

- regrid 前后重叠 valid 区 DDF 保持一致；
- 新增 fine valid 区与 A-B 的 `RemakeDdfState()` 结果一致；
- 删除/合并/拆分 Fab 后无旧 layout 元数据残留；
- 至少跨越两次 regrid 的单 GPU和多 MPI smoke；
- 再进行逐层 valid DDF norm，而不是只观察作业未崩溃。

历史验证使用现已删除的确定性动态打标钩子，按四相循环触发布局 A、布局 B、无细层、
布局 A。jobs `584483`（OSI）和 `584484`（A-B）在 2 MPI ranks 下完成 10 个 coarse
steps，finest level 实际经历 `1 -> 0 -> 1`；18 条 `(step,level)` 的 `active_sum` 与
全部 `active_q0...active_q26` 逐项一致。最终 job `584617` 以相同动态序列和 A-B
checkpoint 对两个 level 的 D3Q27 做逐单元比较，并覆盖新建 fine level 的非平衡缩放
、常驻 `density/velocity` 复用、始终使用 sparse fine patch，以及重构后延迟 ghost
填充；54 项记录全部通过，最大 active
`Linf=1.054711873e-15`、最大 active mean-L1 `=1.492743922e-16`、最大 active
relative-L2 `=2.417721326e-15`。粗层统计必须排除被 fine 完全覆盖、按设计不演化的
coarse cells。
两 rank 共享单 GPU，因此 multi-GPU/node 和动态 AMR 性能仍不在此验收范围内。
当前 `ErrorEst()` 只保留涡量物理判据；原静态/动态几何打标参数及专用提交、检查脚本
均已删除，后续动态回归需建立新的物理判据可复现输入。

## 10. 阶段 7：checkpoint、restart 与输出

状态：**已完成第一版实现及静态两层跨 MPI 分解 restart 验收，
并覆盖静态 `ParticleContainer` AoS/SoA checkpoint。动态 regrid 后 restart、
运动刚体 host 状态、完整 IBM 耦合和 multi-node 尚未覆盖。**

目标：持久化可移植的 canonical DDF。

第一版 checkpoint：

```text
twisted live state
-> gather canonical checkpoint buffer
-> VisMF::Write(canonical)
```

restart：

```text
VisMF::Read(canonical)
-> allocate current BoxArray/DM
-> scatter target_phase=0
-> phase[lev]=0
-> rebuild caches
```

header 增加 layout/version 字段，旧 A-B checkpoint 应有明确兼容策略。不要静默把没有
layout 标记的 checkpoint 当成 twisted 数据。

验收：

- OSI 连续运行与中途 checkpoint/restart 的逐层 valid DDF 对比；
- 不同 MPI 分解 restart；
- plotfile 和 ComputeMacro 不读取 raw twisted 坐标；
- checkpoint 文件可明确辨认布局版本。

实际实现与验收记录：

- 新 checkpoint header 使用 `LBMCheckpointV2`，下一行明确写入
  `canonical_osi_single_array_v1` 或 `canonical_ab_two_array_v1`；历史
  `LBMCheckpoint` 只按明确的旧 A-B canonical 格式读取，不会被解释为 raw OSI；
- OSI 写出只为每层构造一份 `nGrow=0` canonical valid DDF，不写 raw phase；restart
  根据当前 MPI 数重建 DistributionMapping，经 `ParallelCopy` 重分布后写入 phase-0
  `osi_state`，最后重建同层和粗细 ghost/cache；
- velocity plotfile 继续由 OSI-aware `ComputeMacro()` 生成普通 canonical 宏观量后写出；
- job `584619` 完成单层 1-rank checkpoint 到 2-rank restart，逐单元 D3Q27
  `Linf=0`；增强 job `584620` 使用真实涡量判据建立两层全覆盖 AMR，同样按 1-rank
  连续 16 步、1-rank 第 8 步 checkpoint、2-rank restart 至第 16 步进行比较，level
  0/1 和全局 `Linf` 全部为 0，并成功生成 `plt000016`；
- 综合 job `584621` 重复上述两层 OSI 验证，并追加 A-B V2 checkpoint 的 1-rank
  写出与 2-rank restart；OSI 与 A-B 两条 restart 路径的全局 DDF `Linf` 均为 0；
- 增强 job `584839` 在 OSI checkpoint/restart 中加入 50,444 个静态拉格朗日点：
  1-rank 写出、2-rank 重启后，粒子数、坐标和 10 个 SoA 属性校验和在
  `1e-9 + 1e-13*scale` 并行归约容差内一致，两层 D3Q27 的全局 `Linf=0`。
  AMReX 粒子 checkpoint 不会自动序列化 `LagrangeParticleContainer` 的 host 侧
  `centre` / 速度 / 角速度 / 力与力矩，因此本项不构成运动粒子或 IBM 验收；
- `scripts/submit_osi_stage7_restart.sh` 和
  `tests/check_osi_stage7_restart_log.sh` 固化该验证。

## 11. 阶段 8：端到端内存与性能优化

阶段 3 已对同层通信做了为控制同步缓冲和 Decode/Encode 成本所必需的局部优化，包括
分量分批、精确通信区域缓存和 rank-local `TagVector` 融合。这些结果只证明当前单层
全周期路径可行，不代表阶段 8 的端到端优化已经开始或完成。跨物理边界、AMR、regrid
和 restart 的整体优化仍须等阶段 1--7 的数值验证完成后再进行。

依次评估：

1. 保持当前条件分配：生产 OSI 路径不分配 `f_old/f_new`，同时保留可构建的 A-B
   对照模式；
2. 评估是否将当前分批 canonical same-level communication 替换为 OSI-aware
   pack/unpack，并用同一作业内的配对 A/B 验证收益；其范围包括 OSI-aware
   `FillBoundary` 和 `ParallelCopy`，目标是同时减少 canonical 临时内存与通信转换；
3. 将 coarse/fine gather/scatter 融合进现有 direct interpolation/restriction kernel；
4. 用 per-direction head/offset 替代热路径中的整数 `%`；
5. 检查 AMReX component layout、warp 合并访问、寄存器和 local-memory spill；
6. 单列 canonicalization、OSI 地址计算、MPI 和 regrid reset 计时。

论文的 2Q 访存与 A100 GLUPS 不能直接作为 BOX3D_OSI 结果。AMR active cells、ghost、
coarse/fine 传输和 canonicalization 都必须计入端到端性能。

## 12. 文件级改动地图

| 文件                   | 计划改动                                                               | 第一责任阶段 |
| ---------------------- | ---------------------------------------------------------------------- | -----------: |
| `src/OsiIndex.H`     | 无状态 host/device Fab-local 地址 helper                               |  1（已实现） |
| `src/Kernels.H`      | fused OSI collision、直接 state 边界重建和粗细传输 kernel       |         2--5 |
| `src/AmrCoreLBM.H`   | stream mode、level phase/state、通信缓冲和 adapter 接口          |         2--7 |
| `src/AmrCoreLBM.cpp` | launch、通信、边界、粗细传输、AMR/restart 生命周期              |         2--7 |
| `src/main.cpp`       | A-B/OSI 调度选择，regrid 前后规范化边界                                |         2、6 |
| `config/inputs`      | `lbm.stream_mode` 及说明                                             |            2 |
| `tests/`             | 地址置换、A/B norm、MPI/regrid/restart 检查                            |         1--7 |
| `scripts/`           | 固定参数 A/B 提交与日志汇总                                            |         0、8 |

## 13. 最小测试矩阵

| 层次     | 网格/并行            | 边界   | 主要检查                   |
| -------- | -------------------- | ------ | -------------------------- |
| 地址     | 小 Fab，CPU          | 无     | 排列、恒等式、越界         |
| 单层     | 单 Fab，1 GPU        | 周期   | 逐 q DDF                   |
| 同层     | 多 Fab，1 GPU        | 周期   | Fab seam                   |
| MPI      | 多 Fab，2+ rank      | 周期   | face/edge/corner halo      |
| 物理边界 | 单层，1 GPU          | cavity | 边界 DDF/宏观量            |
| 静态 AMR | level 0--1，1/2 rank | 周期   | per-level phase、插值/限制 |
| 动态 AMR | 至少两次 regrid      | 非周期 | canonicalize/reset         |
| restart  | regrid 后 checkpoint | 非周期 | 连续/重启等价              |
| 性能     | 固定 checkpoint A/B  | 相同   | 内存、kernel、端到端 MLUPS |

## 14. 结果记录规范

每个实验至少记录：

- git commit；
- `stream_mode`、`collide_mode`、`interp_mode`；
- 输入文件和 checkpoint；
- GPU、MPI rank、BoxArray/active-cell 统计；
- 编译是否成功；
- 作业是否完成；
- DDF `linf/rel_l2`；
- 是否检查守恒量或基准剖面；
- 性能窗口和计时同步方式。

报告时使用以下证据词汇：

```text
implemented       仅表示代码存在
static checked    仅表示静态检查通过
compiled          仅表示目标构建通过
smoke completed   仅表示短作业完成
numerically equal 必须给出比较范围和 norm
faster            必须给出受控 A/B 配置和测量值
```

## 15. 已收敛选择与待决事项

阶段 3 已用代码和测试收敛以下事项：

1. `osi_state` 的环形 Box 使用完整 grown Fab，并以 `nGrow=2` 分配；
2. MPI 同步当前 phase 下同一逻辑坐标的重叠副本；
3. canonical grown 缓冲按 `osi_sync_batch_components` 分批复用，不保留完整
   Q-component canonical DDF；
4. A-B reference 仅由 `verification.osi_ab_check=true` 条件化分配。

阶段 6 又收敛了两项选择：只有新建或布局改变的 level 重置 phase；动态两层逐单元
DDF norm 已由 job `584617` 完成。仍待后续阶段收敛：

1. checkpoint 使用分块 canonical 输出还是保存 raw state、phase 与布局元数据；
2. IBM 所需 Euler/Lagrange stencil 的可信 ghost 范围与同步时机；
3. multi-GPU/multi-node 回归和受控动态 AMR 性能 A/B。

在这些事项有实现证据前，应继续标记为“待验证设计选择”，不能写成当前行为。
