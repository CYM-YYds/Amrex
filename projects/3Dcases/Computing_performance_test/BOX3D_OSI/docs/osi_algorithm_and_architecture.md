# BOX3D_OSI 算法原理与 AMReX 集成架构

本文是 `BOX3D_OSI` 的权威设计说明。后续实现若改变这里定义的状态不变量、phase
推进时机或 canonicalization 契约，必须同步修改本文。第 1 节的早期作业列表及
第 16 节的设计验收条件保留历史时间语境；当前验证范围见[交接状态](current_status.md)。

> 通信/平均的历史实现描述见相应日期；现役函数行为和最新逐点诊断以
> [当前交接状态](current_status.md) 为准。

## 2026-10-20 维度兼容契约

统一实现以 `AMREX_SPACEDIM` 为唯一维度来源，`LbmLattice.H` 提供 D2Q9 或
D3Q27/order=0 速度集。OSI 仍用三坐标内部表示；二维 z 坐标和偏移为 0、
z 环长度为 1，不分配额外 z 单元。phase 推进、canonicalize/rebuild/reset 和
host-staging 同步契约不变。二维平均四个子单元，三维八个。

checkpoint 的 canonical DDF 之外新增独立 `Lattice` 元数据；读取维度相关场和
参考 DDF 前校验维度、Q 与排列。没有该元数据的旧 checkpoint 仅允许三维
D3Q27/order=0。二维静态圆盘另校验粒子 Geometry；不表示二维 IBM 已启用。
合并、构建入口及当前逐值验证范围见 [README](../README.md#二维与三维统一入口) 和
[合并记录](ai_changes/2026-10-20-merge-dimension-support.md)。下文历史作业的结论
仍限定于各自冻结版本。

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

## 4. phase 状态机

现役 OSI 推进由 `AdvanceLevel()` 编排，phase 表示以下时间状态：

```text
进入 AdvanceLevel(lev)，OSI 布局：
    osi_phase[lev] = p
    storage 表示 t 时刻迁移后的 incoming DDF

1. Collision：
    在 grown Fab 的有效推进范围（排除深层 covered、保留 interface）
    按 A_q(x,p) 同址读取、碰撞、写回 post-collision DDF

2. Same-level MPI/ghost synchronization：
    从 owner logical valid 的 A_q(x,p) 一次解码完整 Q=27 的 post-collision DDF
    按同一逻辑坐标同步接收 Fab 的 ghost 副本，并编码到 A_q(x_ghost,p)

3. phase commit：
    osi_phase[lev] = p + 1
    这一步在逻辑上完成内部 streaming

4. Physical boundary reconstruction：
    从新 phase 的内部逻辑地址 A_q(x_i,p+1) 读取迁移后参考值
    按标准非平衡外推重建本层 uncovered 物理边界的完整 Q 分量
    直接写入 A_q(x_b,p+1)

离开 AdvanceLevel(lev)，OSI 布局：
    storage 的活跃区域再次表示迁移和物理边界处理后的 incoming DDF
```

上述1/2是逻辑阶段；开启[整块重叠](#61-整块碰撞与同层传输重叠)后，
发送源碰撞先完成，剩余块碰撞可以与传输并行。每个owner值仍在其碰撞后才被打包，
本地复制和unpack排在剩余碰撞之后。接收值仍写当前 phase `p`，再由 phase
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
串行路径由 `CommunicateLevel()` 依据 `DdfLayout` 分派；OSI重叠路径由
`AdvanceOneLayout()` 直接传回调给同一通信实现。两者均由
`FillBoundaryOsi()` 直接按当前 phase 完成同 rank raw-to-raw 和跨 rank
pack/unpack。AMReX `getFB()` 的任务在建层时转换为本地、发送和接收 tag，
`FillBoundaryCache` 按层保留计划与独立缓冲；布局重建前先使引用旧 `MultiFab`
的 tag 失效。`MpiPlan`、`MpiBuffers`、peer offset、tag 构建和缓冲索引与
粗细层 CPC 共用辅助实现。每个tag的MPI缓冲采用 `[q][cell]`，收发端一致，
消息字节数、Q和区域不变。`osi_local_direct`、`osi_parallel_copy`、
`osi_mpi_direct` 仅兼容旧输入中的值 1，值 0 会报错。

可选的 `osi_mpi_pipeline_chunk_bytes` 将每个 peer 的 host-staging payload 分块：
发送侧在一块 D2H 完成后立即投递非阻塞 send，与下一块 D2H 重叠；
接收侧先投递全部 receive，每块到达后在独立 GPU stream 上 H2D，主机继续
等待后续块。当前 `config/inputs` 使用 2 MiB；0 表示整块同步路径。2 MiB
的较新证据包括单节点2/4 ranks及动态AMR，详见[当前状态](current_status.md)。
不同peer拓扑、消息规模和多节点仍需单独验证，不能将2 MiB称为普遍最优值。

实验开关 `osi_mpi_device_direct` 将 MPI 指针从 pinned host 缓冲切换为 device 缓冲，
并要求 AMReX 已确认 GPU-aware MPI。远端请求投递后才启动本地 seam copy，使该 kernel
与 MPI wait 重叠；unpack 仍排在同一 CUDA stream 后方，保持本地和远端 ghost 写入顺序。
当前配置尚未确认 HMPI/UCX 支持该 device transport，默认使用 host staging；
更换传输环境后仍需单独验收，不能将 host-staging 结果当成 device-direct 证据。

历史 canonical fallback 的源区和目标区曾由独立的逻辑 `Box` 缓存构造：目标 grown ghost 反向周期平移后与
`BoxArray` valid 相交，得到 source box，再平移回 destination ghost。每个 Fab 的候选
Box 必须先去重，否则同一 canonical cell 会被并发写入。缓存不保存 raw OSI 地址，
因为 raw 地址随 phase 改变。运行时用 AMReX `TagVector` 将本 rank 全部小 Box 融合为
每批一次 Decode launch 和一次 Encode launch；逐 Box launch 已实测会让启动开销淹没
区域裁剪收益。

两层 ghost 还承担 AMR 2:1 子循环的有限时间保护区：初始完整时，最大格速为 1 的
D3Q27 连续推进两步后只保证 valid 仍可信，外层 wrap 污染可能已进入 ghost。因此两步
后必须刷新或丢弃 ghost；若 average-down、IBM 或边界需要读取演化后的 ghost，必须
单独证明其可信范围或增加同步。

### 6.1 整块碰撞与同层传输重叠

`lbm.osi_collide_comm_overlap` 启用后，FB发送tags的 `srcIndex` 唯一决定发送源Fab分类；
`send_fabs` 和是否存在两个非空子集的标志由 `BuildOsiCommunicationRegionCache` 更新，
随 `FillBoundaryCache.clear()` 失效。分类只依赖布局，不随phase变化。
接收、本地复制属性可以与发送属性重叠，不另建互斥块类别。

1. 碰撞所有远端发送源Fab，范围和原kernel不变，然后pack并同步，保证发送数据完整。
2. `FillBoundaryOsi` 的回调将其余Fab碰撞排到stream 0；该子集用
   `MFItInfo.DisableDeviceSync().UseDefaultStream()`，不在MFIter退出或Collide末尾等待。
   CPU继续推进既有staging/MPI；分块D2H/H2D使用stream 1。
3. 本地seam copy及远端unpack排在stream 0的剩余碰撞之后，避免先写ghost再碰撞。
   阶段完成后才提交整层phase；MPI请求仍在一次FillBoundaryOsi调用内完成，没有跨步骤状态。

两个子集只改变顺序，碰撞cell并集、mask判断和写回公式不变，每层调用计数仍按一次推进累加。
只有发送源或只有剩余块时，以及开关关闭时，沿用原顺序。整块同步staging和细分计时的
同步可能减少隐藏机会；验证通过不意味着这些路径有同等加速。

默认值、计时口径及性能使用入口见[README](../README.md#同层通信重叠与计时)，
数值与性能验收见[当前状态](current_status.md)。

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

每次 `AdvanceLevel(lev)，OSI 布局` 成功完成后只增加 `osi_phase[lev]`。在一个粗步内可能出现：

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

### 判据打标、建块与覆盖统计

三维 `ErrorEst()` 调用 `state_error_6`，二维调用 `state_error_3`。
当前三维以 valid 单元的涡量模 W 打标：
`min(1, log2(W)) >= -2 - (max_ref_level - lev)`，编译期 `max_ref_level=3`，
所以 level 0→1、1→2 的阈值分别是 0.03125、0.0625。
运行时 `amr.max_level=2` 限制实际层数，不会改变上述映射；`lbm.err` 数值不参与
当前三维比较，但其数组长度仍用于 ErrorEst 的层级入口检查。统一层号含义属于待决数值变更。

AMReX 26.06 在 ErrorEst 后按顺序缓冲 tags、按
`max(1, blocking_factor[lev+1]/ref_ratio[lev])` 粗化 tags，再执行投影、
嵌套和周期处理，在粗化标记图上按 grid_eff 聚类，恢复尺度后限制 max_grid_size。
TagBox::coarsen 在每个分组内取最大 tag（CLEAR=0、BUF=1、SET=2），不计算原始达标比例。
当前 ratio=2：BF32 对应父层 16³ 分组，BF16 对应 8³ 分组；这是标记图的粒度约束，
不是把流场粗化，也不保证一个原始 tag 单独生成一个细块。
grid_eff=0.7 约束的是处理后的聚类效率，不保证细网格内 70% 的原始父单元满足涡量判据。

`verification.refinement_diagnostics=true`（默认关闭）在缓冲和投影前统计
实际用于 ErrorEst 的 valid 涡量和原始 tag，含 covered 粗单元、不含 ghost；
同次 regrid 后复用 REGRID_DIAG/CF_MASK_DIAG 记录最终网格和覆盖。
常规 Mesh Information 只描述最终网格，不能代替原始打标统计；现有诊断也不输出 tag 空间掩码。
诊断包含额外 GPU 归约和 MPI 同步，不能直接用于无诊断的性能结论。

逐父层令 T 为原始打标数，C 为同次 regrid 后细单元数除以细化比的维度次幂
（三维 ratio=2 时除以 8）。覆盖放大指标 `E=C/T-1` 以原始需求为分母；
`F=1-T/C` 以实际覆盖为分母。T=0 时 E 未定义，C=0 时 F 未定义。
二者是计数指标，不是原始 tag 与最终覆盖的空间集合差，也不证明额外覆盖没有精度收益；
不跨层直接累加不同分辨率的单元数。实验结果统一见
[当前交接状态](current_status.md#2026-10-21加密覆盖与续跑终态)。

### 布局与 phase 的保留/迁移

以下为提交 `af3b5da` 后的现行路径。`RemakeLevel()` 遇到相同 `BoxArray` 和
`DistributionMapping` 时保留该层 raw state 与 phase。布局变化时重置新布局的
phase，并在重叠区按旧 phase 迁移逻辑值；不构造整层 canonical DDF：

```text
regrid 前完整 AverageDownValid
        -> 按各粗层当前 phase 只修复 covered valid 物理边界
        -> Remake 布局不变: 保留旧 raw state 和 phase
        -> Remake 布局变化: ParallelCopyOsi 经 CPC 本地/MPI 标签迁移旧 fine valid 到新布局 phase 0
        -> Remake 新增区: FPinfo patch 与目标 Fab 求交 -> 稀疏 coarse staging
                         -> InitializeDdfFromCoarse 直接写所需 fine valid，phase 0
        -> MakeNew: InitializeDdfFromCoarse 按 fine valid 建立稀疏 coarse stencil
                    -> 按 coarse 当前 phase 搬运 Q 分量，延拓 stencil 的物理域外来源
                    -> 非平衡缩放与自定义插值核 -> 新 fine valid，phase 0（不填 fine ghost）
        -> Clear: 释放该层 OSI state、通信缓冲和地址 tags
        -> 重建通信、插值、restriction 和 coarse-fine mask 缓存
        -> Cycle2 FillGhostLevel(fine): 只建立两步细层子循环需要的 coarse-fine ghost
        -> AdvanceLevel: OSI 每层碰撞后、phase 提交前由 FillBoundaryOsi 同步同层/周期 ghost
```

DDF 初始化、ghost 填充和重构迁移统一使用自定义插值核；AMReX 插值器只提供
DDF stencil/FPinfo 几何。宏观量与力场的 AMReX 插值不属于本次替换范围。
数学公式见 [Kernels.H](../src/Kernels.H)，工作区范围见[粗细层传输说明](#11-静态-coarse-fine-传输)。

宏观量路径也采用 `AverageDownValid() -> ComputeMacroLevel()`，
其中 `AverageDownValid()` 内部完成当前态物理边界修复，因此输出和收敛检查读取的是
完整同步并修复平均改写边界的当前逻辑 DDF；这一步会修改当前 DDF 边界，不是纯只读诊断。

仅在布局变化时重置 phase。迁移保持逻辑值的设计关系如下；
`af3b5da` 当时仅完成编译；当前有限窗口动态重构验证见
[当前交接状态](current_status.md)。
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
`FillBoundaryOsi()` 覆盖。
jobs `584483`
（OSI）和 `584484`（A-B）在 2 ranks、10 coarse steps、每 2 步 regrid 下覆盖
Remake、Clear 和 MakeNew；18 条 active D3Q27 checksum 逐项一致。job `584617` 对上述
最终生命周期执行 54 项逐单元 active D3Q27 检查，最大 `Linf=1.054711873e-15`。

## 10.1 完整平均后的边界修复

粗细覆盖掩码固定构建，不再读取 `lbm.cf_mask_mode`。该参数只用于解释历史配置，不再影响当前计算。
`AverageDownValid()` 从最细层向下完整平均所有 covered 父单元，完成可选的修复前
检查回调，再调用 `RepairCurrentStatePhysicalBoundary()`；宏观量计算和 regrid 前
使用相同入口。平均混合细层边界与内部子单元，因此结果一般不再满足粗层壁面公式。

| 调用 | `repair_after_average` | 边界筛选与实际写入范围 |
|---|---|---|
| 正常推进 `Boundary()` | `false` | 有细层时跳过 `covered != 0`；处理当前 Fab 的 grown 区域内所有非周期物理面，包含域内 ghost |
| 平均后修复 | `true` | 仅遍历 `lev < finest_level`，工作盒裁剪到当前 Fab valid；跳过 `covered == 0` |

`ApplyPhysicalBoundaryLevel()` 复用同一非平衡外推核。工作盒裁剪到非周期物理面，
`fill_boundary_impl()` 仍检查非周期方向的 `0/hi` 壁面坐标；因此修复实际只写
**covered 粗层 valid 与非周期物理边界的交集**。无需细层 BoxArray 粗化或临时求交列表。
单层没有平均目标，修复不写状态；最细层、uncovered、内部格点和 ghost 均不被该修复改写。
A-B 修复 `f_old`，OSI 修复当前 phase 的 `osi_state`；锁步参考态采用同一范围。

执行上，边界缓存构建入口为本层本地 Fab 建立 `boundary_work_cache`，
以两组 `TagVector` 分别缓存正常推进和 valid 修复任务；每次边界调用批量提交对应任务。
任务只保存几何与索引，执行时取得当前数组、读取现有 OSI 相位表，支持 canonical
双缓冲交换；regrid 和清层使缓存失效。在 `grow(valid, nghost) & domain` 中直接
固定非周期物理面坐标，并去除面、棱、角的重叠；不按 valid 贴壁方向再裁剪，
域外 ghost 不参与，内部邻点由边界线程读取。
当前范围及逐值验证见[域内整面记录](ai_changes/2026-10-15-boundary-full-faces.md)。
[批量执行](ai_changes/2026-10-15-boundary-batch.md)和
[薄面配对实验](ai_changes/2026-10-15-boundary-batch-thin.md)仅为扩大 ghost 范围前的性能证据。

这不是新建层/重构插值的通用边界重建接口，也不刷新平均后的同层 ghost。
当前验证确认范围与现有公式一致，并与修改前基线逐值等价；边界离散精度阶数未验证。
运行证据统一见 [当前交接状态](current_status.md#2026-10-09-固定覆盖掩码与平均后边界修复)。

## 11. 静态 coarse-fine 传输

普通 ghost 插值缓存现要求父层 valid 并集与周期像提供完整模板，物理域外来源按
边界延拓映射后检查；域内缺口在缓存建立时终止。该约束由
`AmrCoreLBM_amr.cpp` 内部辅助函数执行，A-B 与 OSI 共用。详细规则、报错处理及
检查范围见 [粗细层传输说明](#11-静态-coarse-fine-传输)。

即使 BoxArray 不变，细层 ghost 填充和 fine-to-coarse restriction 也跨越两个具有不同
phase 的 level。当前实现使用按传输范围构造的 canonical staging 隔离跨层数据布局：

```text
coarse raw（当前 phase）-> ParallelCopyOsi -> coarse_stage（canonical）
-> 物理边界填充 -> scaling/interpolation
-> 直接写 fine logical ghost 的当前 phase raw 地址
```

以及：

```text
fine raw（当前 phase）-> 融合 scaling/restriction，直接读取子单元
-> interface_result（稀疏 canonical 粗单元）
-> ParallelCopyOsi -> coarse 当前 phase raw 地址
```

插值 staging 覆盖所需粗层源区，界面平均结果区覆盖稀疏界面；完整 valid 平均则使用
coarsen(fine valid) 的局部结果区。二维一次处理Q=9、三维一次处理Q=27。
界面平均与完整 valid 平均都通过 `RestrictDdfToCoarse<UseOsiAddress, StoreScaledFine>`
直接读取当前细层数据，不再为 OSI 完整解码细层和粗层。界面任务只覆盖稀疏界面，
完整 valid 任务覆盖所有 fine valid 盒粗化后的父单元，不能互换任务范围。
正常 A-B/OSI 推进都调用 `FillDdfGhostFromCoarse`，按布局装入同一 canonical stage，
再由 `InterpolateDdfStageBatch<UseOsiAddress>` 共用批量缩放和插值提交。
任务由细层的 `interpolation_cache[lev].work` 持有：mode 0 按 fine 目标单元，
mode 1/2 按 coarse parent 派发并裁剪到目标盒。
正常推进和重网格初始化共用 `BuildInterpolationWork` 与 `InterpolateDdfStageBatch`。
正常推进将任务保存在层缓存；初始化按本次请求 patch 构造局部任务，并在任务/暂存区销毁前同步。
初始化目标为 canonical 或 phase=0 OSI，使用同一 canonical 写入特化；不覆盖 fine ghost 或请求区外的 valid。
插值准备阶段的物理域外延拓仍按需要填充的 Box 启动。

CUDA 界面平均使用 `average_down_cache[coarse_lev].block_tags` 合并所有本地 interface Box 提交。
界面及完整 valid 平均的两种布局共用映射和数值函数，`load_ddf<UseOsiAddress>` 在编译期选择直接读取或 OSI 地址映射；
结果先写入各自范围的 canonical buffer，再分别用 `ParallelCopy` 或 `ParallelCopyOsi` 写回粗层。
映射由编译期 Q 确定：Q27 每 16 lane 处理一个父单元、128 线程/block，每 block 最多 8 个父单元；
Q19 每线程处理一个父单元、128 线程/block。默认模型仍为三维 Q27、二维 Q9，不提供运行时 Q19 切换。
Q27 半 warp 每 lane 保存 q=lane 及 q=lane+16 两槽，分别对每个细子单元归约 rho/动量、缩放后累加，
两个半 warp 使用独立 shuffle 掩码，尾部父单元整组退出。
二维 Q9 当前进入非 Q27 的 `average_down_to_canonical` 标量核分支，但
`restriction_group_size=32`、256 线程/block 的任务分组未同步改变：同组 32 个线程取得相同父单元，
各自执行完整 Q 循环并写回，存在重复写入风险。这是当前源码的静态事实；当前二维 CUDA
平均路径的数值正确性仍 pending，历史整 warp 归约验收不能覆盖该分支变化。
缓存建表与执行共用 `restriction_parents_per_block` 及对应 group/thread 常量。
任务只绑定几何和布局索引，执行时取当前数组与 phase，避免 A-B 双缓冲交换后访问旧数组。
CPU 的两种平均范围保持逐盒提交，共用 `average_down_to_canonical` 标量核。
`AverageDownValidLevel(..., DdfLayout)` 创建函数局部的 coarsen(fine valid) canonical 结果区，
复用 `BuildRestrictionWork` 与共同执行器，再按布局写回，保留 uncovered 粗单元。
AB 完整平均使用 `StoreScaledFine=true`，同时将各子单元缩放后的 DDF 写入 `f_new` valid，
保留原实现的双缓冲状态行为；界面平均及 OSI 不写此辅助区。
完整平均的临时搬运计划、任务、结果在 GPU/MPI 完成后才释放，不进入持久界面缓存。
平均后的边界修复仍调用既有薄面批量任务。

正常推进的持久任务分别由 `BuildInterpolationCache`/`BuildAverageCache` 派生，
`ClearOsiTransferCaches` 在任一侧布局/存储改变前使其失效，随后由既有粗细缓存重建流程恢复。
初始化和完整 valid 平均复用相同建表函数，但任务及暂存区由当前调用持有；
调用返回或替换底层存储前先完成 GPU/MPI 同步，不依赖持久缓存失效来管理其生命周期。
缩放任务持有stage的Array4；计算标签保存几何与本地索引，执行时取得当前数组和共享
`FabPhaseCache`偏移，phase变化不触发几何重建，也不缓存独立相位副本。
正式实现没有映射实验开关，默认格子仍为三维Q27、二维Q9；选择Q19编译时自动使用标量映射，
不新增运行时模型选择。其余映射和按q展开插值仅保留在实验快照中。采纳状态及验证边界见
[当前交接状态](current_status.md)。

staging仍增加内存和搬运，其职责是隔离两个问题：

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

## 13. 当前数据结构边界

`AmrCoreLBM.H` 的 `RunMode` 唯一保存 A-B、OSI 生产或 OSI 锁步运行模式；
`streamMode()` 与 oracle 开关由它派生。`DdfLayout` 仍用于单个阶段的布局选择。
每层 OSI 生产数据由 `osi_state` 与 `osi_phase` 持有；`f_old/f_new` 只用于 A-B
生产或锁步参考态。

同层通信的 `FillBoundaryCache` 按层保存本地、pack/unpack tag、`MpiPlan` 和
独立 `MpiBuffers` 及由发送tags派生的整块碰撞分类。粗细层 CPC 缓存复用计划和 tag 构建辅助逻辑，但各传输保留
自己的缓冲。所有持有 `Array4` 的 tag 都必须在所引用 `MultiFab` 释放或替换前
失效；布局建立后重新构建。phase 改变时读取当前值，不重建几何任务。

文件职责和实际推进链统一见 [README](../README.md#源码职责与数据所有权)。
`OsiIndex.H` 的 `MakeOsiFabContext(ring, phase)` 提供无状态几何和相位映射，
不拥有 level 状态；`OsiCommunication.H` 提供通信执行辅助。共享计时器和
checkpoint 常量位于 `AmrCoreLBM.H`，没有独立 detail 头文件。

OSI 通信/缓存搬运的嵌套细分计时由 `RuntimeParams` 中一次读取的
`performance.osi_detail_timing` 控制；代码缺省保留，但当前三维inputs显式关闭。关闭时跳过计时器及其同步，
保留显式数据依赖同步和主阶段计时；日志明确标记未测量，不能把零值解释为没有搬运成本。
用法和配对结果见[README](../README.md#构建运行与实验记录)。

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

## 16. 早期设计完成判据（历史）

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
