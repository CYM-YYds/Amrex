# BOX3D AMR 粗细网格通信

本文档说明当前 BOX3D 中分布函数（DDF）的粗细网格传输路径。它描述的是
AMReX 的 patch-based AMR 实现；不要把它与 Jaber 论文中 GPU-native 八叉树的
显式邻接表实现混为一谈。

## 运行时路径

当前固定物体算例由 `JaberCycle()` 驱动。对每一对相邻层，它的传输顺序是：

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
patch 建立粗层源并调用 `CellConservativeLinear` 插值。

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

`AverageDownGhostLevel()` 创建一个带两层 ghost 的 fine 临时 `MultiFab`，复制
`f_old[lev+1]`，按反向比例缩放其非平衡部分，再调用 `amrex::average_down()` 写回
`f_old[lev]`。它没有维护“只沿界面”的显式 cell 列表；AMReX 根据 fine BoxArray
覆盖到的粗层区域完成 restriction。

## 与 Jaber 论文的对应关系

Jaber 论文的 `Nskip` 是八叉树 block 邻接表中的显式哨兵值：细 block 某个方向的
同层邻居为 `Nskip` 时，该 block 被标记为需要 coarse-fine 通信。论文还维护 cell
mask，将 cell 分为 ghost、interface 和 interior。

当前 BOX3D 没有 `Nskip`、block 邻接表或 cell mask。它通过 `BoxArray` 的覆盖关系
隐式找出待补 patch，并由 AMReX 缓存 `FPinfo` 等通信元数据。两者的物理目的相同，
但网格拓扑表示和工作列表不同。

论文线性插值中的 `F00`、`F10`、`F01`、`F11` 是局部单位方形四角的粗层场值采样点，
不是四个粗 block 的 ID，也不是某个 fine ghost 的唯一“父网格”。二维公式的局部
坐标为 `-1/4 + I/2`，`I = 0..3`；一个 2x2 粗单元采样模板对应 4x4 个 fine 子单元
位置。新建 fine 子单元需要由粗层插值初始化；已经存在的 fine interior cell 则直接
推进，不会每个时间步再由粗层重建。

## 当前实现的性能边界

`FillPatchTwoLevels()` 仅为所需 coarse-fine patch 插值，但当前 `FillDdfPatch()` 的
`interp_scale` 循环会先缩放整层粗网格 valid cells。若将来优化，应在 regrid 后构建
并复用包含插值 stencil halo 的粗层工作列表；不能只按 fine ghost 的几何范围裁剪，
否则会遗漏 `CellConservativeLinear` 的相邻粗单元斜率数据。

相关源码入口：

- `src/main.cpp`: `JaberCycle()`
- `src/AmrCoreLBM.cpp`: `FillDdfPatch()`、`FillGhostLevel()`、`AverageDownGhostLevel()`
- `amrex-26.01/Src/AmrCore/AMReX_FillPatchUtil_I.H`: `FillPatchTwoLevels_doit()`
- `amrex-26.01/Src/Base/AMReX_FabArrayBase.cpp`: `FabArrayBase::FPinfo`
