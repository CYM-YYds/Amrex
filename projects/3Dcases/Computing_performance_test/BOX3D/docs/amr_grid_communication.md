# BOX3D AMR 粗细网格通信

本文档说明当前 BOX3D 中分布函数（DDF）的粗细网格传输路径。它描述的是
AMReX 的 patch-based AMR 实现；不要把它与 Jaber 论文中 GPU-native 八叉树的
显式邻接表实现混为一谈。

## 运行时路径

当前方腔流算例由 `JaberCycle2()` 驱动。对每一对相邻层，它的传输顺序是：

```text
粗层 -> 细层：FillGhostLevel(lev + 1, time, true)
              -> FillDdfPatch()
              -> FillPatchTwoLevels()

细层 -> 粗层：AverageDownGhostLevel(lev, true)
              -> average_down()
```

`FillGhostLevel()` 将目标细层的 `f_old[lev]` 传给 `FillDdfPatch()`。后者以
`f_old[lev-1]` 为粗层源，先将非平衡部分缩放到 `f_new[lev-1]` 临时缓存，再将
该缓存作为 `FillPatchTwoLevels()` 的 coarse source。

缩放使用：

```text
f^scaled = f^eq + (f - f^eq) * tau_f / (2 * tau_c)
```

其中代码中的 `tau` 是本地格子单位下的量；`1/2` 来自相邻层的 2:1 时间步比。

## 哪些细层区域真正使用粗层插值

`FillDdfPatch()` 不是“用粗层覆盖全部 fine ghost”的简单赋值。AMReX 的
`FillPatchTwoLevels()` 首先构造待补区域：

```text
grow(目标 fine Box, nghost) - fine source 的 BoxArray 覆盖区域
```

这个差集由 `FabArrayBase::FPinfo` 缓存。它包含细层自身没有同层有效数据、但为
计算所需的区域；在非物理边界处，这正是 coarse-fine ghost 区。AMReX 只为这些
patch 建立粗层源并调用配置的插值器。当前 DDF 路径在 `FillDdfPatch()` 中选用
`cell_bilinear_interp`；代码中保留了被注释的 `cell_cons_interp` 对照实现。其他
AMReX 填充路径是否使用保守线性插值，不能据此推断。

同一个 fine ghost 位置最终的来源取决于其位置：

| 位置 | 最终来源 |
| --- | --- |
| 有相邻 fine valid cell 的同层边界 | 同层 fine 数据 |
| 周期边界 | 周期映射的 fine 数据 |
| 物理边界 | 物理边界条件 |
| 缺少同层 fine 来源的 coarse-fine 边界 | 缩放后的粗层插值 |

因此，粗细交界 ghost 的插值公式只取粗层 DDF；邻近 fine valid 值不参与该插值。
但 fine 数据仍参与整个填充操作，用于覆盖同层和周期来源。代码先复制粗插细的
临时 patch，随后以 `FillPatchSingleLevel()` 补入可由 fine source 获得的区域。

## 细到粗平均

`AverageDownGhostLevel()` 创建 `nGrow=0` 的 fine 临时 `MultiFab`，只复制
`f_old[lev+1]` 的 valid cells，按反向比例缩放其非平衡部分，再调用
`amrex::average_down()` 写回 `f_old[lev]`。虽然函数名保留了 `Ghost`，但当前调用并不把
fine ghost cell 作为 restriction 源；`average_down()` 根据 `S_fine.boxArray()` 的 valid
范围构造粗化区域，并从每个父 coarse cell 对应的 8 个 fine valid 子 cell 求平均。

它没有维护“只沿界面”的显式 cell 列表；AMReX 根据 fine BoxArray 覆盖到的粗层区域完成
restriction。因此被 fine valid patch 覆盖的 coarse 区域会被平均结果回写。

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
`RebuildCoarseFineMasks()` 会按 level 和全局 Fab 索引缓存与物理域表面相交的 disjoint
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

`FillPatchTwoLevels()` 仅为所需 coarse-fine patch 插值，但当前 `FillDdfPatch()` 的
`interp_scale` 循环会先缩放整层粗网格 valid cells。若将来优化，应在 regrid 后构建
并复用包含插值 stencil halo 的粗层工作列表；不能只按 fine ghost 的几何范围裁剪，
否则会遗漏当前插值器所需的相邻粗单元 stencil 数据。

当前粗层 `covered_mask` 已能跳过大部分完全被细网格覆盖的 Collide/Stream 单元，但仍以
完整 launch box 加逐 cell 分支实现。Boundary 工作箱说明了另一条可行路径：在 regrid
后生成稀疏工作区域，并在多个时间步复用。是否将同一模式用于 coarse-fine interface，
需要先验证工作箱数量、kernel launch 开销以及 Stream 所需 ghost 源区域。

相关源码入口：

- `src/main.cpp`: `JaberCycle2()`
- `src/AmrCoreLBM.cpp`: `FillDdfPatch()`、`FillGhostLevel()`、`AverageDownGhostLevel()`
- `amrex-26.01/Src/AmrCore/AMReX_FillPatchUtil_I.H`: `FillPatchTwoLevels_doit()`
- `amrex-26.01/Src/Base/AMReX_FabArrayBase.cpp`: `FabArrayBase::FPinfo`
