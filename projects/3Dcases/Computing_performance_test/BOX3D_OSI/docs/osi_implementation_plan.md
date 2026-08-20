# BOX3D_OSI 实施与验证计划

本文把 [OSI 算法与 AMReX 集成架构](osi_algorithm_and_architecture.md) 转换为可执行的
开发步骤。它是计划，不是完成记录；只有满足本节验收条件后，才能将对应状态改为完成。

## 1. 当前基线

截至 2026-08-19，`BOX3D_OSI` 的生产推进路径仍与 `BOX3D` 基线一致：

```text
f_old + f_new
Collide -> Communicate -> Stream -> Boundary -> Swap
```

本目录的复制状态由基线提交 `6578093` 固定；代码修改前的文档基线为 `7a929e9`。
两个提交的说明均为 `before codex`。

阶段 1 已经完成：

- `src/OsiIndex.H` 实现 Fab-local host/device 地址 helper；
- `tests/osi_index_test.cpp` 和 `tests/run_osi_index_test.sh` 提供独立 CPU 测试；
- CPU 测试通过；MPI+CUDA 完整构建通过；
- 地址 helper 尚未进入生产 kernel，不构成 OSI 数值实现。

当前没有：

- `lbm.stream_mode`；
- `osi_phase`；
- OSI collision/communication；
- canonical gather/scatter；
- OSI checkpoint 或 regrid 适配；
- OSI 时间推进的运行、数值或性能结果。

## 2. 总体开发策略

必须保留 A-B 基线，通过运行时参数做同代码树 A/B：

```text
lbm.stream_mode = 0  # A-B，初始默认
lbm.stream_mode = 1  # OSI，实验路径
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

验收记录：`./tests/run_osi_index_test.sh` 通过；`MAKE_J=2 GEN_CCDB=0
./scripts/compile.sh` 的 MPI+CUDA 完整构建通过。默认并行度 16 的首次全量构建因编译
节点内存不足失败，降低并行度后成功。此阶段不宣称 LBM 正确。

## 5. 阶段 2：单 Fab、单层固定网格 OSI

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

## 6. 阶段 3：同层多 Fab 与 MPI

目标：实现 OSI-aware logical boundary pack/unpack。

通信接口应表达逻辑区域，不暴露发送端 raw 地址：

```text
Pack(logical_send_box, q, phase, sender_fab)
MPI
Unpack(logical_ghost_box, q, phase, receiver_fab)
```

第一版可发送完整 Q 分量，确认正确后再裁剪为穿过该面的方向。面对角和体对角方向需要
覆盖 edge/corner ghost；可以使用分方向 staged exchange，也可以一次构造完整 halo
通信计划，但必须通过小网格追踪测试验证 corner 来源。

不能对 twisted state 直接调用普通 `FillBoundary()`。如先采用 correctness 脚手架，则：

```text
twisted -> canonical temp
-> FillBoundary
-> scatter logical ghost back to twisted current phase
```

验收：

- 相同全局网格的 1-rank/2-rank/多-rank DDF 对比；
- 改变 Box 分解而不改变全局问题时结果一致；
- 专门检查跨面、跨边、跨角的 D3Q27 分量；
- MPI 结束后、phase 提交前，logical ghost 的值与发送端 logical valid 匹配。

## 7. 阶段 4：非周期物理边界

目标：保持当前 BOX3D 边界公式，只替换地址解释。

步骤：

1. collision 和 MPI 使用旧 phase `p`；
2. 提交 `phase=p+1`；
3. 在新 phase 地址上执行与 A-B `Boundary(f_new)` 相同的公式；
4. boundary work boxes 仍是 logical Box，不缓存 raw OSI 地址。

验收：

- 单层 lid-driven cavity 与 A-B valid DDF/速度/密度对比；
- 六个面、十二条边、八个角分别有覆盖；
- boundary cache 经重新分块后仍只依赖 logical geometry；
- 不使用 `%` 地址循环冒充物理周期边界。

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

验收：

- `osi_phase[lev]` 的增量与 `JaberCycle2()` 子循环次数一致；
- 固定 BoxArray 下 A-B/OSI 每层 valid DDF 在容差内；
- mode 0/1/2 coarse-to-fine 和 average mode 基线至少选择一个固定模式完成对照；
- coarse/fine 传输测试不依赖 raw twisted `ParallelCopy()`。

## 9. 阶段 6：动态 regrid

目标：在同步点执行 canonicalize/rebuild/reset。

建议调用链：

```text
BeforeRegridCanonicalize()
    -> gather every active level valid DDF
    -> establish canonical state/view

AverageDownValid()
RefineMesh()
    -> existing RemakeLevel/RemakeDdfState on canonical data

AfterRegridInitializeOsi()
    -> allocate new osi_state on new BoxArray/DM
    -> scatter canonical new state at phase 0
    -> fill required logical ghost
    -> phase[changed levels] = 0
    -> rebuild layout-dependent caches
```

必须明确未变化 level 是否也统一重置。第一版建议在同步点把所有 active levels 都规范化
并重置，减少 coarse/fine phase 组合；优化阶段再缩小范围。

验收：

- regrid 前后重叠 valid 区 DDF 保持一致；
- 新增 fine valid 区与 A-B 的 `RemakeDdfState()` 结果一致；
- 删除/合并/拆分 Fab 后无旧 layout 元数据残留；
- 至少跨越两次 regrid 的单 GPU和多 MPI smoke；
- 再进行逐层 valid DDF norm，而不是只观察作业未崩溃。

## 10. 阶段 7：checkpoint、restart 与输出

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

## 11. 阶段 8：内存与性能优化

只有阶段 1--7 的数值验证完成后才进入。

依次评估：

1. 删除生产 OSI 路径的 `f_new`，但保留可构建的 A-B 对照模式；
2. 将 canonical same-level communication 替换为 OSI-aware pack/unpack；
3. 将 coarse/fine gather/scatter 融合进现有 direct interpolation/restriction kernel；
4. 用 per-direction head/offset 替代热路径中的整数 `%`；
5. 检查 AMReX component layout、warp 合并访问、寄存器和 local-memory spill；
6. 单列 canonicalization、OSI 地址计算、MPI 和 regrid reset 计时。

论文的 2Q 访存与 A100 GLUPS 不能直接作为 BOX3D_OSI 结果。AMR active cells、ghost、
coarse/fine 传输和 canonicalization 都必须计入端到端性能。

## 12. 文件级改动地图

| 文件 | 计划改动 | 第一责任阶段 |
| --- | --- | ---: |
| `src/OsiIndex.H` | 无状态 host/device Fab-local 地址 helper | 1（已实现） |
| `src/Kernels.H` | fused OSI collision、boundary accessor | 2--4 |
| `src/AmrCoreLBM.H` | stream mode、level phase/state、adapter 接口 | 2--7 |
| `src/AmrCoreLBM.cpp` | launch、通信、canonicalization、AMR/restart 生命周期 | 2--7 |
| `src/main.cpp` | A-B/OSI 调度选择，regrid 前后规范化边界 | 2、6 |
| `config/inputs` | `lbm.stream_mode` 及说明 | 2 |
| `tests/` | 地址置换、A/B norm、MPI/regrid/restart 检查 | 1--7 |
| `scripts/` | 固定参数 A/B 提交与日志汇总 | 0、8 |

## 13. 最小测试矩阵

| 层次 | 网格/并行 | 边界 | 主要检查 |
| --- | --- | --- | --- |
| 地址 | 小 Fab，CPU | 无 | 排列、恒等式、越界 |
| 单层 | 单 Fab，1 GPU | 周期 | 逐 q DDF |
| 同层 | 多 Fab，1 GPU | 周期 | Fab seam |
| MPI | 多 Fab，2+ rank | 周期 | face/edge/corner halo |
| 物理边界 | 单层，1 GPU | cavity | 边界 DDF/宏观量 |
| 静态 AMR | level 0--2 | 非周期 | per-level phase、插值/限制 |
| 动态 AMR | 至少两次 regrid | 非周期 | canonicalize/reset |
| restart | regrid 后 checkpoint | 非周期 | 连续/重启等价 |
| 性能 | 固定 checkpoint A/B | 相同 | 内存、kernel、端到端 MLUPS |

## 14. 结果记录规范

每个实验至少记录：

- git commit；
- `stream_mode`、`collide_mode`、`interp_mode`、`average_mode`；
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

## 15. 当前待决事项

这些问题必须在对应阶段实现前由代码和测试收敛，本文不预设未经验证的答案：

1. 循环 Box 最终使用完整 `fabbox`，还是为 DDF 单独构造固定 halo ring；
2. 第一个 MPI 版本采用 canonical communication 还是直接 pack/unpack；
3. corner halo 使用分方向 staged exchange 还是一次通信计划；
4. regrid 时重置所有 active levels，还是只重置布局发生变化的 level；
5. canonical checkpoint 是否替换当前 `f_old/f_new` 双文件格式；
6. 何时以及如何保留 A-B reference，同时释放 OSI 模式下的第二套 DDF 内存。

在这些事项有实现证据前，应继续标记为“待验证设计选择”，不能写成当前行为。
