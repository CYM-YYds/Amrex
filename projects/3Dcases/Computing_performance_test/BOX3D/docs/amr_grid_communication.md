# BOX3D AMR 粗细网格通信

本文档说明当前 BOX3D 中分布函数（DDF）的粗细网格传输路径。它描述的是
AMReX 的 patch-based AMR 实现；不要把它与 Jaber 论文中 GPU-native 八叉树的
显式邻接表实现混为一谈。

## 运行时路径

当前方腔流算例由 `JaberCycle2()` 驱动。对每一对相邻层，它的传输顺序是：

```text
粗层 -> 细层：FillGhostLevel(lev + 1, time, true)
              -> FillDdfGhostFromCoarse()
              -> direct staging

网格重构：RemakeLevel()
              -> RemakeDdfState()
              -> FPinfo temporary patch

细层 -> 粗层：AverageDownGhostLevel(lev, true)
              -> fused LBM restriction
              -> ParallelCopy()
```

`FillGhostLevel()` 调用 `FillDdfGhostFromCoarse()` 填充目标细层的 `f_old[lev]`。它以
`f_old[lev-1]` 为粗层源，先复制到
按 fine owner 布置的 `interp_direct_coarse_stage[lev]`，再执行粗 DDF 缩放和
`interp_bilinear_d3q()`，直接写入 fine work box。`RemakeLevel()` 不调用此函数；它由
`RemakeDdfState()` 用临时 `FPinfo` patch 完成旧 fine valid 数据迁移和新增区域初始化。

缩放使用：

```text
f^scaled = f^eq + (f - f^eq) * tau_f / (2 * tau_c)
```

其中代码中的 `tau` 是本地格子单位下的量；`1/2` 来自相邻层的 2:1 时间步比。

## 哪些细层区域真正使用粗层插值

`FillDdfGhostFromCoarse()` 不是“用粗层覆盖全部 fine ghost”的简单赋值。它先构造待补区域：

```text
grow(目标 fine Box, nghost) - fine source 的 BoxArray 覆盖区域
```

这个差集由 direct cache 的 `fine_ba_simplified.complementIn(target)` 描述。它包含细层自身
没有同层有效数据、但为计算所需的区域；在非物理边界处，这正是 coarse-fine ghost 区。
`RemakeDdfState()` 的临时 `FabArrayBase::FPinfo` 则描述新布局所需的 valid 与 ghost patch。
两条路径都使用 `cell_bilinear_interp` 和同一非平衡 DDF 缩放。

同一个 fine ghost 位置最终的来源取决于其位置：

| 位置                                  | 最终来源             |
| ------------------------------------- | -------------------- |
| 有相邻 fine valid cell 的同层边界     | 同层 fine 数据       |
| 周期边界                              | 周期映射的 fine 数据 |
| 物理边界                              | 物理边界条件         |
| 缺少同层 fine 来源的 coarse-fine 边界 | 缩放后的粗层插值     |

因此，粗细交界 ghost 的插值公式只取粗层 DDF；邻近 fine valid 值不参与该插值。
但 fine 数据仍参与整个填充操作，用于覆盖同层和周期来源。direct mode 先复制粗层
stencil 到 `coarse_stage` 并写入 fine work box，随后以 fine `FillBoundary()` 补入
可由 fine source 获得的区域。

## 细到粗平均

`AverageDownGhostLevel()` 由 `lbm.average_mode` 选择 full/interface-only 与
split/fused 两个维度的实现。当前默认 `average_mode=3`：每次 regrid 后把 fine BoxArray
粗化，并与 coarse `interface_mask` 相交，缓存稀疏且互不重叠的 coarse-parent 工作箱。
工作箱的格点总数必须与 `interface_mask` 的计数完全一致，否则程序立即中止。

融合 kernel 以一个 coarse parent 为工作单元，直接读取对应的 8 个 fine valid children，
在 kernel 内恢复每个 child 的宏观量、缩放非平衡 DDF 并求平均。结果先写入采用 fine
DistributionMap 的稀疏 coarse 缓冲区，再由 `ParallelCopy()` 回写 `f_old[lev]`；真实
coarse level 与 fine 派生缓冲区的布局和 MPI ownership 可能不同，因此该回写在多 GPU
下也可能包含 MPI 数据移动。

该路径不再创建 fine 临时 `MultiFab`，也不再执行整场 `MultiFab::Copy()`、独立
`average_scale` kernel 和通用 `amrex::average_down()`。函数名虽然保留了 `Ghost`，
restriction 源仍然只有 `f_old[lev+1]` 的 valid cells，fine ghost cells 不参与平均。

普通时间步只回写 coarse-fine interface collar。为了避免 regrid/coarsen 后重新暴露的
深层 covered coarse cell 陈旧，`main.cpp` 在每次 regrid 前调用 `AverageDownValid()`，
执行一次完整 fine-valid 到 coarse 的同步。`average_mode=0/1` 仍保留全 fine-valid 路径，
用于语义基线和对照测试；`average_mode=2/3` 使用相同的 interface 工作区域，区别仅在于
缩放与平均是否融合。所有模式的 restriction 源都只取 fine valid cells，不读取 fine
ghost cells。

## 两层 ghost 与推进范围

当前主状态 `f_old` / `f_new` 使用 `nghost=2`。`JaberCycle2()` 的单层顺序为：

```text
Collide -> CommunicateLevel -> Stream -> Boundary -> Swap
```

`Collide()` 的 launch box 是 `growntilebox(nghost) & Geom(lev).Domain()`：物理域外的
ghost 不参与碰撞，但仍保留物理域内的 coarse-fine ghost。`Stream()` 遍历两层 grown box，
但只写入 `growntilebox(nghost - 1)`。因此在 `nghost=2` 时，最外层 ghost 是为内层
pull-stream 提供源数据的只读层，而 valid cell 与第一层 ghost 是 Stream 的写入目标。
当前 `stream()` kernel 直接读取 pull source，不再逐方向检查 source 是否位于 `fabbox`；
其数组安全性依赖这个一层收缩的 launch box 和 Stream 之前的
`CommunicateLevel()` 填充。

`Boundary()` 在 Stream 后覆盖非周期物理边界的目标值。每次初始化或 regrid 后，
`RebuildCoarseFineCaches()` 会按 level 和全局 Fab 索引缓存与物理域表面相交的 disjoint
valid-cell Box；时间推进时只对这些工作箱启动边界 kernel，不再遍历整个 valid patch。
构造工作箱和 kernel 都依据 `Geom(lev).isPeriodicArray()` 跳过周期方向，周期 ghost 则由
`CommunicateLevel()` 的 `FillBoundary(periodicity)` 提供。当前 `main.cpp` 创建 `Geometry`
时硬编码了三个非周期方向，因此 `inputs` 中被注释的 `geometry.is_periodic` 不能单独启用
周期算例。

两层 ghost 的修改已通过 64-step 单 GPU smoke 作业，并完成了 job `571393` 的
64,000-step 单 GPU 方腔流运行；用户检查 plotfile 后未发现可视化异常。该结果排除了
当前配置下的运行时越界和显著可视化回归，但尚未构成守恒量、误差范数或基准剖面的严格
数值等价性验证。

## 与 Jaber 论文的对应关系

Jaber 论文的 `Nskip` 是八叉树 block 邻接表中的显式哨兵值：细 block 某个方向的
同层邻居为 `Nskip` 时，该 block 被标记为需要 coarse-fine 通信。论文还维护 cell
mask，将 cell 分为 ghost、interface 和 interior。

当前 BOX3D 没有 `Nskip` 或 block 邻接表，也没有与论文语义相同、直接标识 fine ghost、
fine interface 和 interior 的细层 `cells_ID_mask`。当前新增的 `covered_mask` 和
`interface_mask` 位于粗层：它们标识被 `lev+1` 覆盖的 coarse cell 以及其中紧邻 uncovered
coarse cell 的条带，用于裁剪粗层 Collide/Stream。粗细 ghost 填充仍通过 `BoxArray` 的
覆盖关系隐式找出待补 patch，并由 AMReX 缓存 `FPinfo` 等通信元数据。两者的物理目的有
交集，但 mask 所属层级、网格拓扑表示和工作列表并不相同。

论文线性插值中的 `F00`、`F10`、`F01`、`F11` 是局部单位方形四角的粗层场值采样点，
不是四个粗 block 的 ID，也不是某个 fine ghost 的唯一“父网格”。二维公式的局部
坐标为 `-1/4 + I/2`，`I = 0..3`；一个 2x2 粗单元采样模板对应 4x4 个 fine 子单元
位置。新建 fine 子单元需要由粗层插值初始化；已经存在的 fine interior cell 则直接
推进，不会每个时间步再由粗层重建。

## 当前实现的性能边界

正常时间推进只处理 direct cache 记录的 coarse stencil Box，而非整层 coarse 网格。
`RemakeDdfState()` 也只对 `FPinfo.ba_crse_patch` 所给出的临时 coarse patch 缩放；它不读取
或复用旧布局的 direct cache。`CellBilinear::BoxCoarsener()` 已把 patch 扩展到三线性 stencil
所需范围，周期映射由 `ParallelCopy()` 完成。

direct cache 在初始建网、每次 regrid 和 restart 后由 `RebuildCoarseFineCaches()` 统一建立。
因此进入正常时间推进的 `FillDdfGhostFromCoarse()` 时，缓存已经就绪，不在热路径中延迟重建。

当前粗层 `covered_mask` 已能跳过大部分完全被细网格覆盖的 Collide/Stream 单元，但仍以
完整 launch box 加逐 cell 分支实现。Boundary 工作箱说明了另一条可行路径：在 regrid
后生成稀疏工作区域，并在多个时间步复用。是否将同一模式用于 coarse-fine interface，
需要先验证工作箱数量、kernel launch 开销以及 Stream 所需 ghost 源区域。

相关源码入口：

- `src/main.cpp`: `JaberCycle2()`
- `src/AmrCoreLBM.cpp`: `FillDdfGhostFromCoarse()`、`RemakeDdfState()`、`FillGhostLevel()`、`AverageDownGhostLevel()`
- `amrex-26.06/Src/AmrCore/AMReX_FillPatchUtil_I.H`: `FillPatchTwoLevels_doit()`
- `amrex-26.06/Src/Base/AMReX_FabArrayBase.cpp`: `FabArrayBase::FPinfo`
