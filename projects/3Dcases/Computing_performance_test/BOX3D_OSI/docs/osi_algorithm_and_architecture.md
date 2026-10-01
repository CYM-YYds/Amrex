# BOX3D_OSI 算法原理与 AMReX 集成架构

本文是 `BOX3D_OSI` 的权威设计说明。后续实现若改变这里定义的状态不变量、phase
推进时机或 canonicalization 契约，必须同步修改本文。

> 通信/平均的历史实现描述见相应日期；现役函数行为和最新逐点诊断以
> [当前交接状态](current_status.md) 为准。

## 1. 状态与边界

截至 2026-09-12：

- 已确定采用 one-step index（OSI）单数组方案；
- 已确定动态 AMR 布局变化时采用 canonicalize/rebuild/reset；
- `src/OsiIndex.H` 已实现无状态 Fab-local OSI 地址 helper，并有独立 CPU 测试；
- `lbm.stream_mode=1` 选择 OSI 单数组实现；BOX3D 的 A-B 双 `MultiFab`
  路径由 `stream_mode=0` 保留为数值基线。当前 `config/inputs` 选择
  `stream_mode=1`；A-B 对照必须显式覆盖为 0。OSI 已接入编译上限内的多层 AMR、
  多 Fab/MPI、全周期或六面非周期边界；当前确认通过的范围仍是两层，三层以上为诊断路径；
- 地址测试、37 步独立 CPU A/B 测试和 MPI+CUDA 构建已通过；job `581325` 在单 rank、
  单 level、单 Fab、全周期条件下完成 32 步生产 GPU A/B 逐步比较，最大 `linf` 为
  `5.551115123e-17`；
- 当前采用 `nGrow=2 + grown-Fab OSI`。同层通信现走 OSI raw 路径；旧版
  canonical fallback 曾按可配置分量数分批 Decode 通信 source valid、由
  `FillBoundary()` 同步同坐标 ghost，再把目标 ghost Encode 回当前 phase；
- jobs `582016`/`582017` 用非均匀初值和 64 Fab 完成单/双 rank 32 步 A/B，最大
  `linf=1.498801083e-15` 且逐步序列一致；
- CPU A/B 额外验证两层 grown 保护环在只初始同步一次后可连续推进两步而不污染 valid；
- 正式 OSI 模式不分配完整 canonical DDF，宏观量由 OSI accessor 直接读取；六面
  非周期边界已接入直接 state 重建；阶段 5 已通过两层递归子循环以及静态布局下的
  1/2-rank 32-step checksum 回归。阶段 6 已实现不使用 `f_old/f_new` 的 direct OSI
  regrid，并通过双 rank 的 Remake/删层/新建层动态 checksum 回归；最终 job `584617`
  还覆盖新建 fine level 的非平衡缩放、常驻宏观量复用以及“regrid 只写 valid、推进层
  再填 ghost”的生命周期，完成两层 D3Q27 逐单元
  active 范数比较，最大 `Linf=1.054711873e-15`、mean-L1
  `=1.492743922e-16`、relative-L2 `=2.417721326e-15`。canonical
  checkpoint/restart 与宏观量 plotfile 已完成静态两层验证；单节点双 GPU 的同层
  MPI direct 已完成受控正确性和性能 A-B，dynamic-regrid restart、multi-node 和动态
  AMR 受控性能 A/B 仍未完成。四层 OSI job `585083` 在第
  128 个 coarse step 的 regrid 后捕获 NaN，因此不能外推两层结论。

OSI 原论文和均匀网格原型位于
[`research/papers/OSI优化计划`](../../../../../research/papers/OSI优化计划/)。论文只验证了
规则网格；动态 AMR 集成属于本算例需要自行完成和验证的工作。

## 2. 术语

| 术语 | 本文含义 |
| --- | --- |
| logical coordinate | LBM/AMReX 使用的物理网格索引 `(i,j,k)` |
| storage coordinate | 某个 DDF 当前实际存放的 Fab 内存索引 |
| canonical layout | `mf(i,j,k,q)` 就是 logical `(i,j,k,q)` 的普通布局 |
| twisted layout | logical 坐标需经 OSI 映射后才能访问的旋转布局 |
| phase | 某一 AMR level 已完成的 OSI 位移次数，不等同于 coarse-level 外循环 step |
| valid cell | 由当前 Fab/level 拥有并推进的逻辑 cell |
| ghost cell | 为同层通信、物理边界或 coarse-fine stencil 提供数据的逻辑 cell |
| canonicalize/gather | 从 twisted storage 恢复 canonical MultiFab |
| scatter/retwist | 把 canonical DDF 写入指定 phase 的 twisted storage；第一版通常写入 phase 0 |

“valid/ghost”是逻辑坐标和所有权属性，不是底层存储槽的永久属性。当前正式 OSI 将
每个 Fab 的 valid 与两层 ghost 一起纳入环形置换。ghost 是其他 Fab valid 或周期像的
重叠逻辑副本；通信同步同一逻辑坐标的当前 phase 值，空间迁移由随后 phase 提交完成。

## 3. 从 A-B 到 OSI

### 3.1 当前 A-B 基线

当前 `Kernels.H::stream()` 执行 pull streaming：

```cpp
fnew(i, j, k, q) = fold(i - e[q][0],
                         j - e[q][1],
                         k - e[q][2], q);
```

单层推进顺序为：

```text
Collide(f_old in place)
-> CommunicateLevel(f_old ghost)
-> Stream(f_old -> f_new)
-> Boundary(f_new)
-> Swap(f_old, f_new)
```

### 3.2 OSI 地址映射

对方向 `q`、逻辑坐标 `x=(i,j,k)` 和 level phase `p`，定义：

$$
A_q(\mathbf{x},p)=
\left(\mathbf{x}-p\mathbf{e}_q\right)\bmod\mathbf{L}.
$$

`L` 不是整个 AMR level 的虚构连续数组长度，而是当前 Fab 参与循环的实际分配 Box
长度。若 `abox` 是该 Fab 的循环区域，则每个方向按局部坐标计算：

$$
A_{q,d}(x_d,p)=lo_d+
\operatorname{pmod}\left((x_d-lo_d)-p e_{q,d},L_d\right),
$$

其中 `lo_d=abox.smallEnd(d)`、`L_d=abox.length(d)`，`pmod` 返回非负余数。

实现中应先做 `p % L_d`，不要使用 `L_d * p` 保证被除数为正；后者在长时间运行中
可能发生整数溢出。

### 3.3 必须保持的存储不变量

在一个 collision 开始前：

$$
M_q[A_q(\mathbf{x},p)] = f_q(\mathbf{x},t),
$$

其中右侧是逻辑 cell 在当前时刻收到的迁移后 DDF。线程对逻辑 cell `x` 执行：

1. 对所有 `q` 从 `A_q(x,p)` 读取；
2. 恢复宏观量并碰撞；
3. 把每个 post-collision DDF 写回同一个 `A_q(x,p)`。

索引恒等式

$$
A_q(\mathbf{x},p+1)=A_q(\mathbf{x}-\mathbf{e}_q,p)
$$

保证 phase 加一后，逻辑 cell `x` 会读到上一步上游 cell `x-e_q` 的碰撞后结果，因而
不需要显式 `Stream(f_old -> f_new)`。

对固定 `q,p`，`x -> A_q(x,p)` 必须是一一映射。只有满足这一条件，GPU 线程对同一
方向的原位写回才不会产生数据竞争。

## 4. 推荐的 phase 状态机

必须明确 phase 表示哪个时间状态。第一版采用以下约定：

```text
进入 AdvanceOsiLevelImpl(lev)：
    osi_phase[lev] = p
    storage 表示 t 时刻迁移后的 incoming DDF

1. Collision：
    在整个 grown Fab 上按 A_q(x,p) 同址读取、碰撞、写回 post-collision DDF

2. Same-level MPI/ghost synchronization：
    从 owner logical valid 的 A_q(x,p) 一次解码完整 Q=27 的 post-collision DDF
    按同一逻辑坐标同步接收 Fab 的 ghost 副本，并编码到 A_q(x_ghost,p)

3. phase commit：
    osi_phase[lev] = p + 1
    这一步在逻辑上完成内部 streaming

4. Physical boundary reconstruction：
    从新 phase 的内部逻辑地址 A_q(x_i,p+1) 读取迁移后参考值
    按标准非平衡外推重建完整边界 DDF
    直接写入 A_q(x_b,p+1)

离开 AdvanceOsiLevelImpl(lev)：
    storage 再次表示迁移和物理边界处理后的 incoming DDF
```

当前每步在碰撞后同步 owner 的 post-collision 值；接收值仍写当前 phase `p`，再由 phase
提交表达 streaming。phase 只能在整层本次碰撞和通信完成后提交一次。不能让不同
Fab、不同 MPI rank 或同一 level 的不同 kernel 各自提前增加 phase。GPU 实现还必须用
同一 stream 的顺序或显式同步保证碰撞和通信已完成，不能只提前修改主机端 phase 元数据。

如果后续边界模型要求在其他时点施加，应先重新证明进入和离开推进函数时的存储不变量，
而不是只移动一行调用顺序。

## 5. Fab-local 循环缓冲区

### 5.1 为什么不能使用全局 level 长度

AMReX level 不是一块全局连续数组。一个全局 logical cell 可能：

- 位于另一个 Fab；
- 由另一个 MPI rank/GPU 拥有；
- 在该 AMR level 上没有分配。

如果按全局 `Domain().length()` 计算地址，单次 DDF 读取可能需要查找其他 Fab 或跨 rank
访问，既不符合 `Array4` 的局部内存模型，也会失去 OSI 的局部 kernel 优势。

因此 OSI 映射应限制在每个 Fab 的已分配 `fabbox` 内。logical 坐标仍使用 AMReX 的
全局整数索引，映射时减去当前 Fab 的 `smallEnd()` 得到局部坐标。

### 5.2 为什么 `Lx/Ly/Lz` 可以因 Fab 而异

每个 Fab 独立维护一个循环置换。不同 Fab 的长度不同，只会使 phase 在各 Fab 中采用
不同的模数；MPI 传输的是带有逻辑坐标/方向语义的数值，不传输发送端地址。接收端使用
自己的 Fab 布局重新计算目标地址。

必须为每次 kernel launch 提供该 Fab 的：

- 循环 Box 下界；
- 三方向长度；
- 当前 level phase；
- logical launch Box；
- D3Q27 离散速度。

### 5.3 循环区域的第一版选择

第一版建议以每个 FArrayBox 的完整分配 Box（valid + `nghost`）作为循环区域，原因是
同层 MPI 和物理/coarse-fine ghost 都必须能按同一映射写入。

该选择已在单层全周期、多 Fab/MPI 范围通过地址测试与逐步 A/B，并已接入
两层 AMR 粗细传输烟测。实现和后续扩展必须继续确认：

- 任意 `q,p` 下所有 logical cell 映射唯一；
- 映射永不越出该 Fab 分配 Box；
- valid 和逻辑 ghost 在同一 phase 不发生别名；
- phase 跨越多个 Fab 长度周期后仍恢复原映射。

## 6. Ghost 与 MPI 的新契约

普通 A-B 布局直接把邻居值写入固定 `ghost(i,j,k,q)`。grown-Fab OSI 中，MPI 语义是
同步重叠副本，而不是执行 `x -> x+e_q` 的跨 Fab streaming：

```text
发送端 logical boundary cell
-> 发送端 A_q(x_send,p)
-> MPI payload value
-> 接收端 logical ghost cell
-> 接收端 A_q(x_ghost,p)
```

接收值必须写入当前 phase，而不是 `p+1`。对于从 ghost 向相邻 valid 传播的方向：

$$
A_q(\mathbf{x}_{valid},p+1)
=A_q(\mathbf{x}_{ghost},p),
\quad
\mathbf{x}_{valid}=\mathbf{x}_{ghost}+\mathbf{e}_q.
$$

因此下一次 phase 的 valid cell 会自然读取通信写入的槽。

不能把 twisted `MultiFab` 直接交给普通 `FillBoundary()` 并期待正确结果，因为
`FillBoundary()` 按相同 `(i,j,k,q)` 复制，而不知道发送和接收 Fab 各自的 OSI 映射。
canonical fallback 一次将完整 Q 分量解码到临时 canonical `MultiFab`，调用一次普通
`FillBoundary()` 后，只把实际通信目标 ghost 编码回同一个 phase。
Decode 只覆盖通信可能读取的 owner-valid 源区；valid 不会从 canonical 缓冲重复回写。
`lbm.osi_sync_batch_components` 已移除。现役 `CommunicateLevel()` 在单 rank 或两个 direct 开关均打开时
选择 raw direct；`osi_local_direct=0`，或多 rank 下 `osi_mpi_direct=0` 时选择上述
fallback。跨 rank direct 使用按 peer 聚合的 host-staging pack/unpack。通信计划、
偏移和缓冲随布局一次构建，pack/unpack 各由一个 `TagVector` kernel 完成。不能直接对
twisted `osi_state` 调用 `FillBoundary()`。
可选的 `osi_mpi_pipeline_chunk_bytes` 将每个 peer 的 host-staging payload 分块：
发送侧在一块 D2H 完成后立即投递非阻塞 send，与下一块 D2H 重叠；
接收侧先投递全部 receive，每块到达后在独立 GPU stream 上 H2D，主机继续
等待后续块。当前 `config/inputs` 使用 2 MiB；0 表示整块同步路径。2 MiB
只在单节点、单 peer 范围内完成验收，多 peer、多节点和多层 AMR 必须重新验收。

实验开关 `osi_mpi_device_direct` 将 MPI 指针从 pinned host 缓冲切换为 device 缓冲，
并要求 AMReX 已确认 GPU-aware MPI。远端请求投递后才启动本地 seam copy，使该 kernel
与 MPI wait 重叠；unpack 仍排在同一 CUDA stream 后方，保持本地和远端 ghost 写入顺序。
当前 HMPI/UCX 不支持该 device transport，因此默认和当前可运行路径仍是 host staging。

这些源区和目标区由独立的逻辑 `Box` 缓存构造：目标 grown ghost 反向周期平移后与
`BoxArray` valid 相交，得到 source box，再平移回 destination ghost。每个 Fab 的候选
Box 必须先去重，否则同一 canonical cell 会被并发写入。缓存不保存 raw OSI 地址，
因为 raw 地址随 phase 改变。运行时用 AMReX `TagVector` 将本 rank 全部小 Box 融合为
每批一次 Decode launch 和一次 Encode launch；逐 Box launch 已实测会让启动开销淹没
区域裁剪收益。

两层 ghost 还承担 AMR 2:1 子循环的有限时间保护区：初始完整时，最大格速为 1 的
D3Q27 连续推进两步后只保证 valid 仍可信，外层 wrap 污染可能已进入 ghost。因此两步
后必须刷新或丢弃 ghost；若 average-down、IBM 或边界需要读取演化后的 ghost，必须
单独证明其可信范围或增加同步。

## 7. 物理边界

OSI 不改变非平衡外推的物理公式。当前 A-B 和 OSI 都在 streaming/phase
提交后使用迁移后的内部分布函数：

$$
f_q(\mathbf{x}_b)=f_q^{eq}(\rho_b,\mathbf{u}_b)
+f_q(\mathbf{x}_f)-f_q^{eq}(\rho_f,\mathbf{u}_f).
$$

OSI kernel 从 `A_q(x_f,p+1)` 读取内部参考，直接写入
`A_q(x_b,p+1)`。由于不再读取旧 phase，也不再使用旧公式中的 post-collision
`fold`，因此不需要 boundary scratch 隔离跨 phase 别名。

地址访问的基本形式仍是：

```text
普通：f(i,j,k,q)
OSI： f(A_q(i,j,k,phase),q)
```

当前 compact boundary helper 在每个 cell 内按 x-low、x-high、y-low、y-high、
z-low、z-high 的固定顺序选择最终面规则，使边和角的覆盖语义确定。

地址取模只是循环存储机制，不会把非周期物理边界自动变成周期边界。`Geometry`、边界
类型和 boundary work boxes 仍决定物理边界；OSI accessor 只决定写入哪个存储槽。

## 8. AMR level phase 与时间子循环

统一 `Cycle2()` 中 fine level 每个 coarse step 推进两次，因此不能使用唯一全局 `step`
作为所有层的 OSI phase。至少需要：

```cpp
amrex::Vector<std::uint64_t> osi_phase;
```

每次 `AdvanceOsiLevelImpl(lev)` 成功完成后只增加 `osi_phase[lev]`。在一个粗步内可能出现：

```text
level 0 phase += 1
level 1 phase += 2
level 2 phase += 4
...
```

phase 是存储元数据，不是物理时间；它可以在 canonicalization 后安全重置为零。时间、
迭代步和 phase 必须分别命名，禁止用一个 `step` 同时承担三种含义。

## 9. AMReX canonical layout 契约

以下 AMReX/BOX3D 操作默认输入是 canonical layout：

- `FillBoundary()` 和 `ParallelCopy()`；
- `FillPatchSingleLevel/FillPatchTwoLevels()`；
- coarse-to-fine 插值；
- `average_down()` 和当前自定义 restriction；
- `RemakeDdfState()`；
- plotfile、checkpoint、DDF 范数比较；
- 任何直接执行 `mf(i,j,k,q)` 的现有 kernel。

第一版不得把 twisted state 直接传给这些函数。需要两个明确适配器：

```cpp
GatherCanonical(lev, logical_box_or_region, canonical_dst);
ScatterCanonical(lev, canonical_src, target_phase);
```

基本语义为：

```cpp
canonical(i,j,k,q) =
    twisted(Addr(fab,q,phase,i,j,k),q);

twisted(Addr(fab,q,target_phase,i,j,k),q) =
    canonical(i,j,k,q);
```

适配器必须明确处理 valid-only、valid+ghost 两种区域；禁止依靠函数名猜测范围。

## 10. 动态 regrid：保留或重建 phase

以下为提交 `af3b5da` 后的现行路径。`RemakeLevel()` 遇到相同 `BoxArray` 和
`DistributionMapping` 时保留该层 raw state 与 phase。布局变化时重置新布局的
phase，并在重叠区按旧 phase 迁移逻辑值；不构造整层 canonical DDF：

```text
regrid 前完整 AverageDownValid
        -> 按各层当前 phase 重建物理边界，包含 covered 边界单元
        -> Remake 布局不变: 保留旧 raw state 和 phase
        -> Remake 布局变化: ParallelCopyOsi 经 CPC 本地/MPI 标签迁移旧 fine valid 到新布局 phase 0
        -> Remake 新增区: coarse OSI -> 稀疏 coarse/fine patch -> 新 fine phase 0
        -> MakeNew: 刷新已有 coarse density/velocity，按 q 批次解码并缩放非平衡 DDF
                    -> CellConservativeLinear -> 新 fine phase 0
        -> Clear: 释放该层 OSI state、同步缓冲和地址 tags
        -> 重建通信、插值、restriction 和 coarse-fine mask 缓存
        -> Cycle2 FillGhostLevel(fine): 只建立两步细层子循环需要的 coarse-fine ghost
        -> AdvanceOsiLevelImpl: 每层碰撞后、phase 提交前同步同层/周期 ghost
```

宏观量路径也采用 `AverageDownValid() -> RepairCurrentStatePhysicalBoundary() ->
ComputeMacroLevel()`，因此输出和收敛检查读取的是完整同步且重新满足物理边界条件的
当前逻辑 DDF；这一步会修改当前 DDF 边界，不是纯只读诊断。

仅在布局变化时重置 phase。迁移保持逻辑值的设计关系如下；
`af3b5da` 的 CUDA + MPI 编译已通过，单/多 rank 动态重构逐值验收仍待完成。
若重构前有：

$$
M_{old}[A_{old}(x,q,p)]=f_q(x),
$$

迁移从旧地址读取 `f_q(x)`，并写入新布局 phase 0 的 `A_new(x,q,0)=x`；
因此目标是让新状态表示同一个 `f_q(x)`。

旧/新重叠区直接使用 CPC 标签迁移；新增区始终建立实际 interpolation patch
所需的稀疏 coarse/fine Q 分量 staging，但不会分配 `f_old/f_new` 或整层 canonical
DDF。Remake/MakeNew/MakeNewFromScratch 均只保证 valid，避免在 AMR 回调中混入 ghost
生命周期。碰撞是逐格点局部操作，因此 level 0 和同层重叠 ghost 不需要预填；fine
碰撞前只插值 coarse-fine 保护区，同层/周期副本随后由 post-collision
`CommunicateOsiLevel()` 覆盖。
jobs `584483`
（OSI）和 `584484`（A-B）在 2 ranks、10 coarse steps、每 2 步 regrid 下覆盖
Remake、Clear 和 MakeNew；18 条 active D3Q27 checksum 逐项一致。job `584617` 对上述
最终生命周期执行 54 项逐单元 active D3Q27 检查，最大 `Linf=1.054711873e-15`。

## 11. 静态 coarse-fine 传输

即使 BoxArray 不变，细层 ghost 填充和 fine-to-coarse restriction 也跨越两个具有不同
phase 的 level。当前实现使用稀疏 canonical staging 隔离跨层数据布局：

```text
coarse twisted -> coarse canonical staging
-> 原有 scaling/interpolation
-> fine canonical result
-> scatter 到 fine logical ghost 的当前 phase
```

以及：

```text
fine twisted valid -> fine canonical staging
-> 原有 scaling/restriction
-> coarse canonical result
-> scatter 到 coarse logical valid 的当前 phase
```

该 staging 只覆盖实际插值或限制需要的区域；现役实现按完整 Q=27 一次处理，仍会增加临时内存和搬运，不代表最终性能方案。它的用途是隔离两个问题：

1. OSI 单层推进是否数值正确；
2. AMR coarse/fine 数值转换是否在 twisted/canonical 边界上正确。

当前 coarse-to-fine 已通过 direct interpolation cache 直接写 OSI ghost，fine-to-coarse
已使用 fused restriction。jobs `584361`/`584362`（1 rank）和 `584363`/`584364`
（2 ranks）在 `amr.regrid_int=-1` 下完成 32 个 coarse steps，level 0/1 的 active
D3Q27 checksum 在 OSI 与 A-B 间逐项一致。两 rank 作业共享一块 GPU，因此只证明
静态两层跨 rank 数据路径，不构成 multi-GPU/multi-node 或性能证据。

## 12. Checkpoint、输出与 restart

当前 checkpoint 持久化 canonical DDF，而不依赖旧 Fab 的 twisted 地址。
header 使用 `LBMCheckpointV2` 并显式记录
`canonical_osi_single_array_v1` 或 `canonical_ab_two_array_v1`。restart 可按当前
DistributionMapping 重分布 valid DDF，将 OSI level 恢复为 phase 0，再重建同层、
粗细 ghost 和布局缓存。plotfile、宏观量计算和 DDF reference comparison
也从 canonical view 或 OSI-aware accessor 读取，不会直接解释 raw twisted MultiFab。

`checkpoint.write_particles=true` 时，粒子容器在新算例中初始化，并通过
AMReX `ParticleContainer::Checkpoint/Restart` 持久化 AoS/SoA 数据。该接口不会
自动序列化 `LagrangeParticleContainer` 中的 host 侧刚体状态，包括质心、
平动/角速度、力和力矩。因此当前只宣称静态粒子容器的 restart 能力，
不宣称运动粒子或完整 IBM 耦合已经通过验收。job `584839` 以 50,444 个
拉格朗日点验证了 1-rank 写出、2-rank 重启：粒子数、坐标和 10 个
SoA 属性校验和在并行归约容差内一致，两层 D3Q27 全局 `Linf=0`。

## 13. 建议的数据结构边界

以下数据边界已经在阶段 2--3 建立；后续阶段仍会扩展其生命周期：

```cpp
enum class StreamMode : int {
    AB = 0,
    OSI = 1
};

amrex::Vector<amrex::MultiFab> osi_state;
amrex::Vector<std::uint64_t> osi_phase;

// 按可配置分量批大小复用的 canonical grown 同步缓冲。
amrex::Vector<amrex::MultiFab> osi_sync_buffer;

struct OsiFabLayout {
    amrex::Dim3 lo;
    amrex::Dim3 len;
};
```

同层通信还维护两类布局相关缓存：`[level][global Fab][Box]` 形式的 Decode/Encode
逻辑区域，以及只覆盖本 rank Fab 的持久 `TagVector<CommunicationTag>`。前者不含 raw
OSI 地址；后者绑定 `osi_state` 的 `Array4`，用于把多个稀疏 Box
融合成每批一次 Decode/Encode GPU launch。因此发生 define、重新分块或销毁布局时，
必须先清除 tags，再释放或重建其引用的 `MultiFab`。

职责划分建议：

| 位置 | 职责 |
| --- | --- |
| `AmrCoreLBM.H` | `AmrCoreLBM` 统一接口、stream mode、每层 phase、grown OSI state 和通信缓存声明 |
| `OsiIndex.H` | 非负取模和无状态 Fab-local host/device 地址映射 |
| `Kernels.H` | fused OSI collision kernel 和 boundary accessor |
| `AmrCoreLBM.cpp` | 构造、参数、输出、checkpoint 和粒子耦合 |
| `AmrCoreLBM_amr.cpp` | 网格生命周期、粗细层缓存、插值和 AMReX 回调 |
| `AmrCoreLBM_osi.cpp` | raw/phase 转换、OSI 通信、OSI 插值和平均下传 |
| `AmrCoreLBM_advance.cpp` | 宏观量、平均、通信分派、碰撞、Stream、边界和推进 |
| `AmrCoreLBM_diagnostics.cpp` | A-B/OSI 对照、source diagnostics、checksum 和收敛监测 |
| `main.cpp` | 根据 stream mode 选择 A-B 或 OSI 推进；保持 AMR 调度一致 |
| `config/inputs` | `lbm.stream_mode`；当前默认 1，A-B 测试必须显式覆盖为 0 |

地址函数必须是无状态 device helper；phase 的所有权属于 level driver，不能在 device
kernel 内自行修改。

## 14. 不能破坏的比较控制

引入 OSI 时只改变 DDF 存储与 streaming 实现，不同时修改：

- D3Q27 方向编号和权重；
- collision 模型或 `collide_mode`；
- 物理边界公式；
- `nghost`；
- coarse-to-fine 插值模式；
- fine-to-coarse restriction 模式；
- refinement criterion、regrid interval 或粒子参数。

否则无法把数值或性能变化归因于 OSI。

## 15. 主要风险

1. **phase off-by-one**：通信和边界重建读 `p`、边界散布写 `p+1` 的状态定义若混乱，会产生看似稳定但方向错位的结果。
2. **直接调用 AMReX copy**：twisted MultiFab 传入 `FillBoundary/ParallelCopy` 会按错误逻辑坐标复制。
3. **Fab 维度变化**：重构后沿用旧 `smallEnd/length` 会越界或静默读错。
4. **层间 phase 不同**：用 coarse step 代替 per-level phase 会在子循环后错位。
5. **raw ghost 清理**：按固定存储下标清 ghost 可能清掉当前映射下的 logical valid。
6. **restart 丢失布局语义**：只保存 raw DDF 而不保存/消除 phase，重启后无法解释。
7. **过早删除 A-B 基线**：没有同输入对照时无法确认错误来自 OSI、AMR 还是物理模型。
8. **把 canonicalization 成本隐藏**：性能统计必须单列 gather/scatter/regrid reset，不能归并后宣称 OSI kernel 提速。
9. **边界时序回退**：若重新改为从旧 phase 或 post-collision `fold` 读取内部参考，将恢复跨 phase raw 槽位别名风险。

## 16. 设计完成的判据

在开始大规模实现前，代码评审至少应确认：

- phase 状态机只有一个解释；
- Fab 循环 Box 的范围已确定；
- 同层 MPI 的 send/receive logical region 已明确；
- 物理边界在 phase commit 后读取迁移后内部值，并写同一新 phase；
- compact boundary helper 的边、角覆盖优先级与 A-B 基线一致；
- coarse/fine 各自的 phase 如何进入传输适配器已明确；
- regrid 已有 direct OSI remap/reset 入口；canonical checkpoint/restart 已完成静态
  两层跨 MPI 分解验证，dynamic-regrid restart 仍待验证；
- A-B 路径仍可在同一源码树中运行；
- 测试已覆盖地址置换、单层数值、MPI、AMR、regrid 和静态 restart；job `591180` 的
  双 GPU、多 Fab、同层 MPI direct 六阶段 64 步 A-B 为 `linf=0`，但多层动态 regrid
  仍未通过同等强度验收。

当前实现、运行结果和待验收范围见[当前交接状态](current_status.md)。
