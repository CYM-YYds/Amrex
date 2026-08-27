# BOX3D DDF coarse-to-fine 填充学习文档

> 适用范围：`projects/3Dcases/Computing_performance_test/BOX3D/` 当前代码。
>
> 当前实现固定使用稀疏 coarse staging 和非平衡缩放；`lbm.interp_mode` 在三线性、
> GPU 守恒线性和 GPU 单元二次插值之间切换。三种模式都要求 ratio=2。
>
> 本文的“插值”是 **AMR 粗层到细层的 DDF ghost 填充**，不是 IBM 粒子与流场之间的 `InterpForce()`。

## 1. 先建立整体图景

### 1.1 它解决什么问题

细层 patch 进行 LBM 碰撞和迁移时，需要读取 valid 区外的 ghost cell。ghost 数据有三个来源：

1. 同层相邻 fine patch 的 valid 数据；
2. 粗细交界处由 coarse DDF 插值得到的数据；
3. 物理边界条件。

`FillDdfGhostFromCoarse()` 把这三类来源按正确优先级写入当前细层的 ghost：

```text
coarse f_old[lev-1]
        |
        | 搬运必要的 coarse stencil
        v
coarse_stage
        |
        | 非平衡 DDF 缩放
        v
scaled coarse DDF
        |
        | interp_mode 选择的 GPU 插值
        v
fine coarse-fine ghost
        v
f_old[lev] 的 coarse-fine ghost
        |
        | Boundary：处理 valid 及域内 Fab ghost 中的物理边界面
        | Collide
        | CommunicateLevel：同层 fine valid 覆盖有 owner 的 ghost
        v
可供 Stream 读取的 fine DDF
```

`FillDdfGhostFromCoarse()` **不负责**碰撞、迁移和 fine-to-coarse 平均下传。平均下传由 `AverageDownGhostLevel()` 处理。

### 1.2 当前时间推进中的调用链

```text
main()
  -> JaberCycle2(0, cur_time, lid)
      -> FillGhostLevel(lev + 1, cur_time, true)
          -> FillDdfGhostFromCoarse(lev + 1, cur_time)
```

`JaberCycle2()` 对每个非最细层先填充下一层 ghost，再推进当前层。下一层以一半时间步连续推进两次，最后才平均回粗层。

### 1.3 当前三种插值模式

正常时间推进调用 `FillDdfGhostFromCoarse()`，使用预构建的 direct cache；
`RemakeLevel()` 调用 `RemakeDdfState()`，使用 `TheFPinfo()` 临时 patch 迁移旧 fine
valid 数据并初始化新增 valid 区域。两条路径采用相同的 coarse 非平衡缩放，并由
`lbm.interp_mode` 选择相同的 stencil 与 GPU kernel：

| 模式 | coarsener | GPU kernel |
| ---: | --- | --- |
| `0` | `cell_bilinear_interp` | `interp_bilinear_d3q()` |
| `1` | `cell_cons_interp` | `interp_cell_cons_linear_children_d3q()` |
| `2` | `quadratic_interp` | `interp_cell_quadratic_children_d3q()` |

## 2. 学习所需的四个文件

| 文件                                | 重点内容                                                      |
| ----------------------------------- | ------------------------------------------------------------- |
| [`main.cpp`](main.cpp)             | `JaberCycle2()` 何时请求 ghost 填充                         |
| [`AmrCoreLBM.H`](AmrCoreLBM.H)     | DDF`MultiFab`、运行模式和 direct cache 的所有权             |
| [`AmrCoreLBM.cpp`](AmrCoreLBM.cpp) | `BuildDirectInterpolationCache()`、ghost 填充和重构迁移逻辑 |
| [`Kernels.H`](Kernels.H)           | `average_scale()` 和 `interp_bilinear_d3q()` 的数值公式   |

推荐在编辑器中按以下顺序跳转：

```text
JaberCycle2
  -> FillGhostLevel
  -> FillDdfGhostFromCoarse
  -> BuildDirectInterpolationCache
  -> average_scale
  -> interp_bilinear_d3q
```

## 3. 参数、容器和所有权

### 3.1 两个函数的职责

```cpp
void AmrCoreLBM::FillDdfGhostFromCoarse(int lev, amrex::Real time);
void AmrCoreLBM::RemakeDdfState(
    int lev, amrex::Real time, amrex::MultiFab& new_old_state);
```

| 函数                       | 目标                | 主要写入区域                              |
| -------------------------- | ------------------- | ----------------------------------------- |
| `FillDdfGhostFromCoarse` | `f_old[lev]`      | coarse-fine ghost、同层/物理边界 ghost    |
| `RemakeDdfState`         | 新布局`old_state` | 迁移旧 valid，并以 coarse 初始化新增 valid |

`RemakeDdfState()` 不接触 direct cache，因此不会把旧布局的 `fine_index` 或 staging Box
误用于新布局。

### 3.2 主要数据容器

| 名称                                       | 拥有者         | 布局                                | 用途                                        |
| ------------------------------------------ | -------------- | ----------------------------------- | ------------------------------------------- |
| `f_old[lev-1]`                           | `AmrCoreLBM` | coarse level`BoxArray/DM`         | 原始 coarse DDF，只读来源                   |
| `f_old[lev]`                             | `AmrCoreLBM` | fine level`BoxArray/DM`           | 正常时间推进的 fine 目标                    |
| `interp_direct_coarse_stage[lev]`        | `AmrCoreLBM` | 按 fine owner 构造的稀疏 coarse Box | coarse stencil 临时容器                     |
| `interp_direct_fine_boxes[lev]`          | `AmrCoreLBM` | `stage_index -> work_box`         | 记录每个 staging Box 要写的 fine ghost 区域 |
| `interp_direct_fine_index[lev]`          | `AmrCoreLBM` | `stage_index -> fine_index`       | 找到目标 fine Box                           |
| `interp_direct_needs_physical_fill[lev]` | `AmrCoreLBM` | 每个 staging Box 一个标志           | 识别 coarse stencil 是否越过非周期物理边界  |

`coarse_stage` 不是完整 coarse level 的副本。它只保存当前 coarse-fine ghost 插值所需的 stencil，并把这些 Box 分配给对应 fine Fab 的 MPI owner。

## 4. 第一层：先看几何区域如何构造

这一层在 `BuildDirectInterpolationCache(lev)` 中完成。它由 `RebuildCoarseFineCaches()` 在初始建网、regrid 或 restart 后统一重建，而不是每个时间步重复构造；正常推进进入填充函数时缓存已经就绪。

4.1 `fine_ba` 与 `fine_ba_simplified`

```cpp
const BoxArray& fine_ba = f_old[lev].boxArray();
const BoxArray fine_ba_simplified = fine_ba.simplified();
```

- `fine_ba` 是真实 fine `MultiFab` 的 patch 布局；
- `fine_ba_simplified` 只是几何覆盖查询用的简化 Box 集合；
- `simplified()` 不会改变 `f_old[lev]` 的存储和并行归属。

### 4.2 `fine_domain`：为什么只扩展周期方向

```cpp
Box fine_domain = Geom(lev).Domain();
for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
    if (Geom(lev).isPeriodic(dir)) {
        fine_domain.grow(dir, fill_ng[dir]);
    }
}
```

周期边界外的 ghost 坐标可以通过周期映射取数，所以目标区域允许越出原始 domain `fill_ng` 层。非周期方向的域外数据应由物理边界处理，因此不在这里扩展。

### 4.3 `target`：valid 区加 ghost 外壳

```cpp
const Box target =
    amrex::grow(fine_ba[fine_index], fill_ng) & fine_domain;
```

如果二维 fine Box 是 `[16,31] x [16,31]`，`fill_ng = 2`，则 grow 后是：

```text
[14,33] x [14,33]
```

再与 `fine_domain` 求交，剔除不合法的非周期域外部分。

### 4.4 `leftover`：真正需要 coarse 插值的区域

```cpp
const BoxList leftover =
    fine_ba_simplified.complementIn(target);
```

核心等式是：

```text
leftover
= 当前 fine Fab 的 valid + ghost 目标区
- 所有同层 fine valid 覆盖区
```

例如，当前 patch 右侧有相邻 fine patch 时，右侧 ghost 将由同层 fine valid 数据提供，不应使用较低分辨率的 coarse 插值。如果上侧没有 fine patch，上侧 ghost 条带则进入 `leftover`。

`leftover` 往往是多个离散 Box，而不是一个大矩形。

### 4.5 `work_box -> coarse_box`

```cpp
for (const Box& work_box : leftover) {
    const Box coarse_box = coarsener.doit(work_box);
}
```

- `work_box` 使用 fine 索引空间，是最终要写入的 ghost 区；
- `coarse_box` 使用 coarse 索引空间，是插值需要读取的 stencil。

`coarsener.doit(work_box)` 不等于简单的 `coarsen(work_box, ratio)`。插值还要读取相邻
coarse cell，所以当前 mapper 的 `BoxCoarsener` 会按其 stencil 扩大读取范围。
cache 构建和 `RemakeDdfState()` 必须使用与 GPU kernel 相同的模式，否则会缺少邻居数据。

### 4.6 为什么按 fine owner 分配 `coarse_stage`

```cpp
const int owner = f_old[lev].DistributionMap()[fine_index];
```

staging Box 被放到目标 fine Fab 所在的 MPI rank/GPU。这样 `ParallelCopy()` 先把 coarse 数据搬到目标设备，插值 kernel 随后就可以读本地 `coarse_stage` 并直接写目标 fine Fab。

在当前 BOX3D 的 coarse/fine 对齐条件下，一个合法 `work_box` 对应一个确定的 coarse stencil；不同 `work_box` 不会产生完全相同的 stencil。因此当前实现不做全局去重，而是让每个 `work_box` 独立对应一个 staging Box。`fine_index` 唯一确定该 fine Fab 的 `DistributionMap()` owner，所以不需要额外保存一份 work-box 列表或 owner 映射。

### 4.7 `needs_physical_fill`

如果 coarse stencil 越过非周期 coarse domain，cache 会记录该 Box 需要物理边界补值。当前实现把域外索引截断到最近的域内 cell，例如：

```text
-1 -> 0
64 -> 63
```

周期方向不做这种截断，而是依赖 `ParallelCopy()` 的周期映射。

### 4.8 几何层的总结

```text
fine_ba[fine_index]
        |
        | grow(fill_ng) 并裁剪到合法 fine_domain
        v
target
        |
        | 减去所有同层 fine valid 覆盖
        v
leftover / fine work boxes
        |
        | mapper->BoxCoarsener(...).doit()
        v
coarse stencil boxes
        |
        | 按目标 fine Fab 的 owner 布置，一一对应
        v
coarse_stage MultiFab
```

到这一步还没有执行 DDF 缩放和插值，只是确定“写哪些 fine cell、读哪些 coarse cell、数据放到哪个 owner”。

## 5. 第二层：每次调用真正做了什么

### 5.1 direct cache 已在时间推进前建立

`FillDdfGhostFromCoarse()` 没有目标 `MultiFab` 参数，写入目标固定为当前 `f_old[lev]`。
它只消费 `RebuildCoarseFineCaches()` 建立的 direct cache。布局迁移是
`RemakeDdfState()` 的职责，不会在该函数内按布局选择另一条路径。

### 5.2 把 coarse DDF 搬到 staging 布局

```cpp
coarse_stage.ParallelCopy(
    f_old_lev_c, 0, 0, Q,
    IntVect(0), IntVect(0),
    Geom(lev - 1).periodicity());
```

数据方向是：

```text
f_old[lev-1] -> interp_direct_coarse_stage[lev]
```

`ParallelCopy()` 不是普通赋值。源和目标的 `BoxArray/DistributionMapping` 不同，它需要完成 Box 交集查找、本地复制、MPI 通信和周期映射。

### 5.3 粗层非平衡 DDF 缩放

每个 staging cell 执行：

```cpp
average_scale(i, j, k, coarse, scale);
```

数值公式是：

$$
f_q^{\text{scaled}}
= f_q^{\text{eq}}(\rho,\mathbf{u})
+ \left(f_q-f_q^{\text{eq}}(\rho,\mathbf{u})\right)s,
\qquad
s=\frac{\tau_{lev}}{2\tau_{lev-1}}.
$$

kernel 先由所有 `Q` 个 DDF 恢复 `rho` 和 `u`，计算平衡态 `feq`，再只缩放非平衡部分 `f-feq`。因此它不是对整个 `f_q` 直接乘一个常数。

`1/2` 与当前 refinement ratio 下细层时间步为粗层一半的调度相对应。

### 5.4 所选 GPU 插值直接写 fine ghost

mode 0 以一个 fine cell 为工作单元；mode 1/2 以一个 coarse parent 为工作单元，计算一次
斜率后直接写入 `fine_work_box` 内的最多 8 个 fine children。coarse-cell-driven 写法避免
为同一父单元的 8 个子单元重复计算斜率，也不需要额外 slope `MultiFab`。

三线性模式调用：

```cpp
interp_bilinear_d3q(i, j, k, fine, coarse);
```

对每个 fine ghost cell 和每个 DDF 方向 `q`，kernel 从 8 个 coarse 点取值：

$$
f_q^f = \sum_{a,b,c\in\{0,1\}}
w^x_a w^y_b w^z_c f_q^c(i_a,j_b,k_c).
$$

ratio=2 时三个方向的权重为 `3/4` 和 `1/4`，组合成
`27/64`、`9/64`、`3/64`、`1/64`。

守恒线性模式使用

$$
f^f=f^c+o_xs_x+o_ys_y+o_zs_z,\qquad o_d\in\{-1/4,1/4\},
$$

其中三个斜率由 minmod 风格限制器得到。完整 8 个 children 的偏移和为零，因此平均值
回到父 coarse 值；限制器能抑制新极值，但数据相关条件判断在 GPU 上有额外成本。

二次模式在上述一次项外增加 `xx/yy/zz/xy/xz/yz` 六个二次项，对应 AMReX
`CellQuadratic` 的笛卡尔 ratio-2 公式。它没有限制器，规则的乘加更适合 GPU，但陡峭
区域可能产生过冲。

### 5.5 插值后的覆盖与边界处理

当前 `FillDdfGhostFromCoarse()` 只负责写 coarse-fine ghost，不再在函数末尾调用
`mf.FillBoundary()` 和 `PhysBCFunct()`。后续时间推进按以下职责处理：

1. `Boundary()` 在物理边界 valid cell 以及域内 Fab ghost 上执行 LBM 边界处理；
2. `Collide()` 更新本层所需区域；
3. `CommunicateLevel()` 用同层 valid owner 覆盖相邻 Fab ghost，并处理周期映射；
4. `Stream()` 读取已经按上述所有权更新的 DDF。

`BuildBoundaryWorkBoxes()` 使用 `grow(valid_box, nghost) & domain` 构造工作面，因此这里的
Fab ghost 仍位于物理索引域内。对于同时具有同层 valid owner 的重叠位置，后续通信会用
owner 数据覆盖；没有同层 owner 的 coarse-fine ghost 则保留插值和边界处理结果。

### 5.6 为什么物理域外 ghost 也可能影响 fine valid

pull streaming 读取 `target - e_q`。当 fine patch 同时靠近物理边界和 coarse-fine 边界时，
源点可能在一个方向越出物理域、在另一个方向又落在 fine valid 区之外。该点既没有同层
fine valid owner，也不是 `Boundary()` 直接修正的 valid cell，却仍可被 fine valid 目标读取。

当前 D3Q27 路径中的一个实测例子是：

```text
目标 valid cell: (126, 7, 96), q=13, e=(-1, 0, 1)
pull source:      (127, 7, 95)
fine validbox:    ((64,0,96) (127,63,127))
fine fabbox:      ((62,-2,94) (129,65,129))
```

源点的 `x=127` 位于物理边界，而 `z=95` 低于 fine Box 的 valid 下界 `z=96`，所以它是
物理边界与 coarse-fine 边界相交处的 ghost。第一细步还可能从物理域外的
`(128,7,94)` 更新它。当前实现通过扩展后的 `Boundary()` 直接处理 `(127,7,95)` 这类位于
物理域内的混合 ghost；若该位置存在同层 valid owner，之后再由 `CommunicateLevel()`
覆盖。仅保留 coarse 插值而不扩展边界工作面会改变后续 valid DDF。

### 5.7 Boundary 扩展到 Fab ghost 的验证与采用结论

曾验证过一种替代组织：把 `Boundary()` 的工作面从 fine valid 扩展到
`grow(validbox, nghost) & domain`，使位于物理域内、同时处于 Fab ghost 区的壁面坐标也
执行 LBM 壁面非平衡外推。64 步单 GPU 对比得到：

| 对比 | Global L-infinity | Global relative L2 | 结论 |
| --- | ---: | ---: | --- |
| 扩展 Boundary vs 原路径 | `1.389699149e-4` | `9.318836925e-6` | 扩展本身改变 valid DDF |
| 扩展 Boundary 且删除尾部两步 vs 保留尾部两步 | `0` | `0` | 在该测试内尾部两步不再影响 valid DDF |

第一组对比还使 level-1 的 AMR tag 数从 `69190` 变为 `69192`，说明扩展工作面不是与旧
路径逐位等价的小优化，而是一种边界数据组织选择。10000 步可视化对比未发现差异，且第二
组范数验证表明，在扩展 Boundary 后，插值尾部的 `FillBoundary/PhysBCFunct` 对 valid DDF
不再产生影响。当前代码据此固定采用扩展 Boundary，并删除了 `boundary_ghost_mode` 切换和
插值尾部两步；旧 mode 0/2 结果只作为这一决策的历史验证证据。

## 6. 用一个最小数值例子理解三线性插值

先把三维简化成一维。设 refinement ratio 为 2，fine cell 的 coarse 基准索引是 `ic`，它位于 coarse cell 的左半部分。根据当前 kernel 的权重公式：

```text
wx = 3/4
ax = 1/4
sx = -1
```

若缩放后的某个 DDF 分量为：

```text
coarse(ic, q)     = 1.0
coarse(ic - 1, q) = 0.6
```

则 fine 值是：

$$
f_q^f = 0.75\times 1.0 + 0.25\times 0.6 = 0.9.
$$

三维情况只是在 x、y、z 方向各选两个 coarse 点，将三个一维权重相乘后对 8 个点求和。

当前代码固定要求 `ratio=(2,2,2)`。三个方向的权重只可能是 `3/4` 或 `1/4`，八个组合
权重直接使用 `27/64`、`9/64`、`3/64`、`1/64`，不再保留通用 refinement-ratio 分支。

## 7. normal ghost 填充与重构迁移两条调用路径

`FillDdfGhostFromCoarse()` 固定写当前 `f_old[lev]` 的 ghost：它使用缓存的 staging Box，
不迁移 valid 数据。`RemakeLevel()` 单独调用 `RemakeDdfState()`：后者先用 `FPinfo` 临时
patch 的 coarse 插值初始化新增 valid 区域和所需 ghost，再以旧 `f_old[lev]` 覆盖可迁移的
fine valid 数据，最后处理物理边界。

## 8. 容易混淆的五个点

1. **DDF coarse-to-fine 填充不是 IBM 插值。** `InterpForce()` 处理拉格朗日粒子与欧拉网格的耦合，两者的数据拥有者、kernel 和物理意义都不同。
2. **`leftover` 不等于全部 ghost。** 它已经减去可由同层 fine valid 提供的部分。
3. **`coarse_box` 不是 `work_box` 的简单粗化。** 它还包含插值 stencil halo。
4. **`coarse_stage` 不是物理状态的新拥有者。** 它是布局缓存与每次调用的短期数据 staging 容器；真正 coarse DDF 仍由 `f_old[lev-1]` 拥有。
5. **数值等价不等于性能更好。** 短程 DDF 范数回归只能证明测试范围内的数值一致性；性能结论需要同一可执行文件、输入、GPU 和输出设置下的 1000 步窗口 A/B 测量。
