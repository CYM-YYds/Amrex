
最快的方式不是从 AMReX 的 `FillPatchTwoLevels()` 源码逐行读起，而是先把 `FillDdfPatch()` 拆成四层：

## 一、先明确它解决什么问题

`FillDdfPatch()` 的任务是：

```text
粗层 DDF
  -> 非平衡部分缩放
  -> 填充细层 coarse-fine ghost
  -> 同层 fine 数据覆盖
  -> 物理边界处理
```

它只处理**粗细网格之间的 DDF ghost 填充**，不负责 LBM 碰撞、迁移或细到粗平均。

当前调用链是：

```text
JaberCycle2()
  -> FillGhostLevel(lev + 1, time, true)
      -> FillDdfPatch(lev + 1, time, f_old[lev + 1])
```

建议先看：

- [main.cpp](/home/huazkjdxmrsgjzdsyshi/whcs-share18/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D/src/main.cpp)
- [AmrCoreLBM.cpp](/home/huazkjdxmrsgjzdsyshi/whcs-share18/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D/src/AmrCoreLBM.cpp:705)

## 二、只学习当前实际使用的路径

当前函数里有三条路径：

### 1. `cf_interp_mode == 0`

```cpp
InterpScale()
FillPatchTwoLevels()
```

这是 AMReX 通用路径，适合先理解原始逻辑。

### 2. `cf_interp_mode == 1`

```cpp
TheFPinfo()
coarse_patch
fine_patch
ParallelCopy()
average_scale()
interp_bilinear_d3q()
ParallelCopy()
FillBoundary()
PhysBC
```

这是当前的稀疏 patch 路径。

### 3. `cf_interp_mode == 2`

```cpp
缓存 fine ghost work boxes
构造 coarse_stage
coarse_stage.ParallelCopy()
average_scale()
interp_bilinear_d3q()
fine FillBoundary()
PhysBC
```

这是 direct 路径，也是目前最值得学习的路径。

### 4. `cf_interp_mode == 3`

```cpp
coarse_stage.ParallelCopy()
interp_bilinear_d3q_scaled()
```

这是实验性的融合缩放路径。早期标记为 mode 3 的测试后来确认实际回退到了通用路径，
没有执行融合 kernel，因此目前不能据此判断它的数值等价性或性能；学习时仍先以经过
逐 DDF 范数验证的 mode 2 为主线。

学习时先忽略 mode 3，重点理解 mode 2。

### 插值缓存在哪里构造

网格布局相关工作不在每次 `FillDdfPatch()` 中重复完成，而是在初始化、regrid 或 restart
后由下面的缓存入口重建：

```text
RebuildCoarseFineCaches()
  -> BuildInterpolationCache()
       mode 0   -> BuildInterpScaleWorkBoxes()
       mode 1   -> 无持久缓存
       mode 2/3 -> BuildDirectInterpolationCache(lev)
```

`BuildDirectInterpolationCache()` 保存 coarse staging Box、fine ghost 工作 Box、目标 fine
Fab 索引和物理边界标记。`FillDdfPatch()` 的时间推进路径只搬运和计算 DDF；仅启动阶段的
特殊层级保留延迟构建回退。

## 三、用数据流理解每个变量

在 `FillDdfPatch()` 开头，先建立这张映射：

```cpp
f_old_lev_c = f_old[lev - 1];  // coarse 原始 DDF
f_old_lev_f = f_old[lev];      // fine 目标 DDF
mf           = fine 目标 MultiFab
coarse_stage = coarse 插值临时数据
```

它们的关系是：

```text
f_old_lev_c
    |
    | ParallelCopy
    v
coarse_stage
    |
    | average_scale
    v
scaled coarse DDF
    |
    | interp_bilinear_d3q
    v
mf 的 fine ghost
```

然后再执行：

```text
mf.FillBoundary()
PhysBCFunct
```

注意：`coarse_stage` 不是完整 coarse level，而只是 coarse interpolation stencil 所需的局部区域。

## 四、重点掌握三个“区域”

### 1. fine target 区域

```cpp
Box target = grow(fine_ba[fine_index], fill_ng) & fine_domain;
```

含义是：

```text
当前 fine Box 的 valid 区
+ nghost 层 ghost
```

在当前代码中：

```text
nghost = 2
```

因此每个 fine Box 都会考虑两层 ghost。

### 2. 同层 fine 可覆盖区域

```cpp
const BoxList leftover =
    fine_ba_simplified.complementIn(target);
```

它表示：

```text
target 中无法由同层 fine valid 数据直接提供的部分
```

这些 `leftover` Box 才需要粗网格插值。

这一步是整个函数最关键的几何逻辑：

```text
fine ghost
  - 同层 fine valid 覆盖
  = 真正需要 coarse interpolation 的区域
```

### 3. coarse stencil 区域

```cpp
coarsener.doit(work_box)
```

它不是简单的：

```cpp
work_box.coarsen(refRatio)
```

因为线性插值还需要相邻粗网格点恢复梯度，所以 AMReX 会扩大 coarse 读取范围。

因此：

```text
fine work_box
  -> coarsener
  -> coarse stencil Box
```

## 五、再理解两个核心计算

### 1. 非平衡 DDF 缩放

代码中的 `average_scale()` 实际执行：

\[
f_q^{scaled}
=
f_q^{eq}
+
(f_q-f_q^{eq})
\frac{\tau_F}{2\tau_C}
\]

对应代码：

```cpp
average_scale(i, j, k, coarse, scale);
```

其中：

```cpp
scale = tau[lev] / tau[lev - 1] / 2.0;
```

这里的 `1/2` 来自细网格时间步是粗网格的一半。

学习时建议先单独看：

- `average_scale()`
- `interp_scale()`
- `feqQian()`

不要一开始就看 AMReX 插值器。

### 2. 三线性插值

当前三维函数是：

```cpp
interp_bilinear_d3q(...)
```

虽然名字里是 `bilinear`，但在三维中实际使用 8 个粗网格点：

```text
F000
F100
F010
F110
F001
F101
F011
F111
```

计算结构是：

\[
f^F_q =
\sum_{a,b,c\in\{0,1\}}
w_{abc} f^C_{q,abc}
\]

对于每个 fine ghost cell：

```text
1. 根据 fine 坐标找到 coarse 基准坐标
2. 判断当前点位于 coarse cell 的哪一半
3. 计算 x/y/z 权重
4. 读取 8 个 coarse stencil 点
5. 对 27 个 DDF 分量分别插值
```

## 六、用一个最小例子学习

假设：

```text
refRatio = 2
fine ghost cell = (5, 5, 5)
```

首先找到粗坐标：

```text
ic = floor(5 / 2) = 2
jc = floor(5 / 2) = 2
kc = floor(5 / 2) = 2
```

然后计算它在粗网格 cell 中的位置：

```text
ioff = 1
joff = 1
koff = 1
```

于是它会读取类似：

```text
(2,2,2), (3,2,2), (2,3,2), (3,3,2),
(2,2,3), (3,2,3), (2,3,3), (3,3,3)
```

每个粗点都有 27 个 DDF 分量，最终得到：

```text
fine(5,5,5,q), q = 0...26
```

## 七、建议的阅读顺序

按这个顺序最快：

1. `main.cpp` 中的 `JaberCycle2()`；
2. `FillGhostLevel()`；
3. `FillDdfPatch()` 的变量初始化；
4. `target` 和 `leftover` 的区域构造；
5. `coarsener.doit(work_box)`；
6. `coarse_stage.ParallelCopy()`；
7. `average_scale()`；
8. `interp_bilinear_d3q()`；
9. `FillBoundary()`；
10. `PhysBCFunct`；
11. 最后再看 AMReX 的 `FillPatchTwoLevels()`。

不要反过来从 `AMReX_FillPatchUtil_I.H` 开始，否则会同时遇到：

```text
FPinfo
FillPatcher
InterpBase
FabArray
ParallelCopy
PhysBCFunct
BoxArray
DistributionMapping
```

很容易把“数据来源”和“调度机制”混在一起。

## 八、我可以怎样具体帮助你

最有效的学习方式是分成四次：

### 第一次：只画数据流

```text
coarse f_old
 -> coarse_stage
 -> scaled coarse
 -> fine ghost
```

### 第二次：只讲区域几何

重点解释：

```cpp
target
leftover
coarsener.doit()
fine_work_boxes
```

### 第三次：只讲数值公式

重点解释：

```cpp
average_scale()
interp_bilinear_d3q()
```

并用一个具体坐标手算 8 点权重。

### 第四次：逐行跟踪一次实际调用

选定：

```text
lev = 1
fine Box = 某个具体 Box
nghost = 2
```

跟踪：

```text
target 是什么
leftover 有几个
coarse stencil 是什么
最终哪些 fine ghost 被写入
```

这样你会同时掌握：

```text
物理意义
几何区域
数据容器
AMReX 调度
GPU kernel
```

而不会陷入 AMReX 源码细节中。

最推荐的下一步是：**先对当前 `cf_interp_mode=2` 路径进行一次逐行讲解，从 `target`、`leftover` 和 `coarsener.doit()` 开始。**


下面只讲 `FillDdfPatch()` 中的**区域构造阶段**，先不进入 `ParallelCopy()` 和 GPU kernel。

核心代码是：

```cpp
const BoxArray& fine_ba = f_old_lev_f.boxArray();
const BoxArray fine_ba_simplified = fine_ba.simplified();
Box fine_domain = Geom(lev).Domain();

for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
    if (Geom(lev).isPeriodic(dir)) {
        fine_domain.grow(dir, fill_ng[dir]);
    }
}
```

以及：

```cpp
Box target = amrex::grow(fine_ba[fine_index], fill_ng) & fine_domain;
const BoxList leftover =
    fine_ba_simplified.complementIn(target);
```

---

## 1. `fine_ba` 是什么

```cpp
const BoxArray& fine_ba = f_old_lev_f.boxArray();
```

`fine_ba` 是当前细层所有 valid patch 的集合。

例如二维中，细层可能有四个 Box：

```text
fine_ba[0] = [0,31]  × [0,31]
fine_ba[1] = [32,63] × [0,31]
fine_ba[2] = [0,31]  × [32,63]
fine_ba[3] = [32,63] × [32,63]
```

这些 Box 表示：

```text
哪些细网格 cell 是 valid cell
```

它们不是 ghost 区域，也不是粗细 interface 标记。

---

## 2. 为什么要 `simplified()`

```cpp
const Box fine_ba_simplified = fine_ba.simplified();
```

它的作用是对 BoxArray 做几何简化，合并可以合并的相邻区域，减少后续几何判断中的 Box 数量。

例如：

```text
原始 BoxArray：

+-------+-------+
| Box 0 | Box 1 |
+-------+-------+

经过 simplified() 后，可能变成：

+---------------+
|   一个大 Box  |
+---------------+
```

它只用于回答：

```text
某个 fine ghost 区域是否已经被同层 fine valid 区域覆盖？
```

这里有一个重要区别：

- `fine_ba`：当前实际 MultiFab 的 patch 布局；
- `fine_ba_simplified`：用于几何覆盖判断的简化布局；
- `coarse_stage`：后面为插值专门创建的临时粗网格布局。

`simplified()` 不会改变 `f_old_lev_f` 的真实存储结构。

---

## 3. `fine_domain` 为什么要扩展周期方向

```cpp
Box fine_domain = Geom(lev).Domain();

for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
    if (Geom(lev).isPeriodic(dir)) {
        fine_domain.grow(dir, fill_ng[dir]);
    }
}
```

通常：

```cpp
Geom(lev).Domain()
```

表示当前层级的物理域索引范围，例如：

```text
[0,63] × [0,63] × [0,63]
```

如果 x 方向周期，细网格位于：

```text
x = 0
```

附近的 ghost cell 越过左边界后，实际上应该对应：

```text
x = 64, 65, ...
```

这些坐标虽然超出原始 `Domain()`，但它们是合法的周期 ghost 范围。

因此周期方向要允许目标区域扩展：

```text
原始 fine_domain：
[0,63]

周期扩展后：
[-nghost, 63+nghost]
```

非周期方向不扩展，因为物理域外的数据应该由物理边界条件处理，而不是被当成普通插值区域。

---

## 4. `target` 表示什么

```cpp
Box target =
    amrex::grow(fine_ba[fine_index], fill_ng) & fine_domain;
```

这一步分两部分。

### 第一步：扩展 fine valid Box

假设：

```text
fine_ba[fine_index] = [16,31] × [16,31] × [16,31]
nghost = 2
```

那么：

```cpp
grow(fine_ba[fine_index], 2)
```

得到：

```text
[14,33] × [14,33] × [14,33]
```

这包含：

```text
valid cell + 两层 ghost
```

### 第二步：与 `fine_domain` 求交

```cpp
... & fine_domain
```

表示只保留合法的细层目标区域。

因此：

```text
target =
当前 fine patch 的 valid 区
+ 需要填充的 ghost 区
+ 周期方向允许的扩展
```

但 `target` 还不是全部需要粗插值的区域，因为其中可能有一部分可以由同层 fine 数据填充。

---

## 5. `leftover` 是整个几何逻辑的核心

```cpp
const BoxList leftover =
    fine_ba_simplified.complementIn(target);
```

可以把它理解为：

```text
leftover = target - 同层 fine valid 覆盖区域
```

也就是：

```text
需要填充的区域
-
已经存在同层 fine valid 数据的区域
=
真正需要粗网格插值的区域
```

举一个二维例子。

假设当前 fine patch 为：

```text
F = [16,31] × [16,31]
```

两层 ghost 后：

```text
target = [14,33] × [14,33]
```

其中：

```text
[16,31] × [16,31]
```

是当前 patch 自己的 valid 区。

如果右侧还有相邻 fine patch：

```text
[32,47] × [16,31]
```

那么当前 patch 右侧的 ghost：

```text
x = 32,33
```

可以从相邻 fine valid 区得到，不需要粗网格插值。

但是如果上侧没有 fine patch：

```text
y = 32,33
```

那么上侧 ghost 就属于 `leftover`，需要从 coarse level 插值。

因此 `leftover` 通常不是一个完整大矩形，而是若干个离散 Box：

```text
leftover:
- 左侧 ghost 条带
- 上侧 ghost 条带
- 下侧 ghost 条带
- 角落区域
```

这就是为什么代码使用：

```cpp
BoxList
```

而不是单个 `Box`。

---

## 6. 为什么不能直接把整个 `target` 粗插值

如果直接对整个 `target` 做粗插值，会有两个问题。

### 问题一：覆盖了更准确的 fine 数据

同层 fine valid 数据通常比 coarse 插值更准确。

正确优先级应该是：

```text
fine valid / fine neighbor
    >
coarse interpolation
    >
physical boundary
```

因此不能让 coarse 插值覆盖已经存在的 fine 数据。

### 问题二：计算范围会膨胀

假设真正需要 coarse 插值的 ghost 只有：

```text
1000 个 cell
```

但整个 `target` 有：

```text
10000 个 cell
```

如果直接插值整个 `target`，就会额外处理 9000 个不必要 cell。

`leftover` 的作用就是把粗插值限制到必要区域。

---

## 7. `work_box` 和 `coarse_box` 的关系

代码中：

```cpp
for (const Box& work_box : leftover) {
    const Box coarse_box = coarsener.doit(work_box);
```

这里：

```text
work_box
```

位于 fine level 坐标系中，表示需要写入的 fine ghost 区域。

```text
coarse_box
```

位于 coarse level 坐标系中，表示插值时需要读取的 coarse stencil 区域。

关系是：

```text
fine work_box
    |
    | coarsener.doit()
    v
coarse stencil Box
```

注意它不一定等于：

```cpp
coarsen(work_box, refRatio)
```

原因是线性插值需要相邻 coarse 点。

例如，一个 fine ghost 区域粗化后只覆盖：

```text
coarse cell (10,10,10)
```

但三线性插值还需要周围的 coarse 点：

```text
(9,9,9) 到 (11,11,11)
```

所以 AMReX 的 `Coarsener` 会根据插值器的 stencil 规则扩大粗网格读取范围。

---

## 8. 为什么还要记录 `owner`

```cpp
const int owner =
    f_old_lev_f.DistributionMap()[fine_index];
```

`owner` 表示这个 fine Box 属于哪个 MPI rank 或 GPU。

之后创建：

```cpp
DistributionMapping coarse_stage_dm(std::move(coarse_owners));
```

意思是：

```text
为每个 coarse staging Box 分配到对应 fine Box 的 owner 上
```

这样做的目的，是让后面的插值 kernel 可以：

```text
在同一个 MPI rank / GPU 上：
读取 coarse_stage
直接写对应的 fine Fab
```

如果 coarse staging 被分配到别的 GPU，就会出现额外的数据通信或无法直接访问目标 fine 数组。

所以这里记录的不是 coarse Box 的物理父子关系，而是：

```text
fine 目标 patch 的并行归属
```

---

## 9. `needs_physical_fill` 是什么

```cpp
bool needs_fill = false;

for (int dir = 0; dir < AMREX_SPACEDIM; ++dir) {
    needs_fill =
        needs_fill ||
        (!Geom(lev - 1).isPeriodic(dir) &&
         (coarse_box.smallEnd(dir) <
              Geom(lev - 1).Domain().smallEnd(dir) ||
          coarse_box.bigEnd(dir) >
              Geom(lev - 1).Domain().bigEnd(dir)));
}
```

它判断：

```text
coarse stencil 是否越过 coarse 物理域
```

例如：

```text
coarse_domain = [0,63]
coarse_box = [-1,10]
```

说明插值 stencil 需要读取：

```text
coarse index = -1
```

这在非周期物理边界上是非法的，因此需要边界处理。

代码将粗网格域外坐标截断到最近的域内坐标：

```cpp
src_i =
    i < coarse_lo.x ? coarse_lo.x :
    i > coarse_hi.x ? coarse_hi.x :
    i;
```

也就是：

```text
-1 -> 0
64 -> 63
```

如果该方向是周期方向，则不执行这种最近点截断，而依赖周期映射。

---

## 10. 当前新增的 coarse stencil 去重

当前代码还做了：

```cpp
for (int candidate = 0;
     candidate < coarse_boxes.size();
     ++candidate) {
    if (coarse_boxes[candidate] == coarse_box &&
        coarse_owners[candidate] == owner) {
        stage_index = candidate;
        break;
    }
}
```

含义是：

```text
如果两个 fine work box：
1. 需要相同的 coarse stencil Box；
2. 属于同一个 MPI owner；

那么共享同一个 coarse_stage。
```

数据结构关系变成：

```text
coarse_stage[stage_index]
    ├── fine_work_boxes[stage_index][0]
    ├── fine_work_boxes[stage_index][1]
    └── ...
```

一个 coarse staging Box 可以对应多个 fine work box。

但测试结果显示，当前算例中这种重复较少，所以没有带来明显加速。

---

## 11. 当前阶段的完整区域数据流

把前面的内容合起来：

```text
fine_ba[fine_index]
        |
        | grow(nghost)
        v
target
        |
        | 减去同层 fine valid 覆盖区域
        v
leftover / fine work boxes
        |
        | Coarsener
        v
coarse stencil boxes
        |
        | 按 fine owner 分配
        v
coarse_stage MultiFab
```

到这里为止，代码还没有做任何 DDF 数值计算，只是在决定：

```text
哪些 fine ghost 要写
哪些 coarse DDF 要读
这些 coarse 数据放在哪个 GPU
```

这是学习 `FillDdfPatch()` 时最重要的第一层：

```text
几何区域构造层
```

下一层才是数据计算层：

```text
coarse_stage.ParallelCopy()
average_scale()
interp_bilinear_d3q()
```

## 当前结论

可以把 `FillDdfPatch()` 的区域逻辑压缩成一句话：

> 对每个 fine patch，先构造 valid+ghost 的目标区域，再去掉能由同层 fine 数据提供的部分，剩余区域转换成 coarse 插值 stencil，并按目标 fine patch 的并行归属建立临时 coarse staging。

下一步应继续看：

```cpp
coarse_stage.ParallelCopy(...)
```

重点理解三个问题：

1. coarse 数据从哪个 `MultiFab` 搬到哪个 `MultiFab`；
2. 为什么需要 `ParallelCopy()` 而不是普通赋值；
3. 多 GPU 时 coarse Box 和 fine Box 的 owner 不一致会发生什么。
