# MLS Direct Forcing IBM 教学说明

本文档用于帮助阅读论文 **A moving least square immersed boundary method for SPH with thin-walled rigid structures** 中的核心算法，并把它和当前单 CPU 示例代码中的 BTDF 方法放在一起比较。

对应代码文件：

- `projects/pdf/Cylinder_IBM_testSPH.cpp`
- `Circle_IBM_BTDF()`: 当前保留的 BTDF 基准方法
- `Circle_IBM_MLSDirect()`: 按论文 3.2 节思想写出的教学版 MLS direct forcing 方法

## 1. 论文要解决的问题

薄壁结构的难点是：边界只有一层结构粒子，壁两侧的流体距离边界都很近。

如果使用传统扩散型 immersed boundary force spreading 方法，在边界粒子上算出力以后，再用核函数把力扩散到附近流体粒子，力的支撑域很容易同时覆盖薄壁两侧。

直观上就是：

```text
薄壁上侧流体      o  o  o
                  ^  ^  ^
                  |  |  |   BTDF / DDF 的力云可能扩散到这一侧
==================B==================  一层结构粒子
                  |  |  |   同一个边界点的力云也可能扩散到另一侧
                  v  v  v
薄壁下侧流体      o  o  o
```

这会造成两类问题：

1. 薄壁两侧互相干涉，本来应该彼此独立的速度信息被同一个边界核混在一起。
2. 边界力不是直接作用在应该被修正的界面流体粒子上，而是先在结构粒子上计算，再扩散出去，边界厚度和核函数选择会强烈影响结果。

论文提出的 MLS direct forcing IBM 的主要思想是：

> 不在结构粒子上算一个边界力再扩散，而是在界面流体粒子上直接计算它应该受到的力。

## 2. 两种粒子的分类

论文 3.2 节把流体粒子分为两类：

```text
远离边界的流体粒子：inner fluid particles
靠近边界的流体粒子：interface fluid particles
```

距离结构表面小于 `d` 的流体粒子被认为是界面流体粒子。论文中 `d` 通常取 `dx` 到 `2dx`。

在当前教学代码中，对应参数是：

```cpp
#define MLS_INTERFACE_DISTANCE (2.0 * dx)
```

它控制的是：哪些流体网格点要被直接强制。

当前代码使用圆柱符号距离：

```text
phi(x, y) = distance_to_circle_surface

phi < 0       : 圆柱内部
0 <= phi <= d : 圆柱外侧界面流体点，需要直接强制
phi > d       : 内层流体点，用作 MLS 插值源
```

## 3. MLS direct forcing 的核心流程

对每一个界面流体粒子 `i`，算法做三件事：

1. 找到它附近的源点。
2. 用 MLS 插值得到它在下一时刻应该具有的期望速度 `u_d`。
3. 用 `u_d - u*` 直接计算体积力。

流程图如下：

```mermaid
flowchart TD
    A[流体一步预测: 得到 preliminary velocity u*] --> B[按距离结构表面 d 分类]
    B --> C{流体粒子 i 是否为 interface particle?}
    C -- 否 --> Z[不施加 IBM 直接力]
    C -- 是 --> D[收集支撑域内源点]
    D --> E[源点1: inner fluid particles 的 u*]
    D --> F[源点2: structure particles 的边界速度 U_d]
    E --> G[构造 MLS 矩阵 A]
    F --> G
    G --> H[计算 MLS 权重 W_MLS]
    H --> I[插值得到界面流体粒子期望速度 u_d]
    I --> J[直接力 f_i = rho0 * (u_d - u_i*) / dt]
    J --> K[把 f_i 加到该界面流体粒子的体积力 Ft]
```

## 4. MLS 插值到底在做什么

普通核插值可以写成：

```text
u_i = sum_j u_j W_ij
```

但在边界附近，粒子分布是不完整、不规则的。尤其薄壁结构只有一层结构粒子时，普通核函数的归一化和方向信息都可能变差。

MLS 的做法是：在目标点 `i` 附近拟合一个局部线性函数，而不是直接相信原始核权重。

二维情况下，可以把局部基函数写成：

```text
p_ij = [1, x_i - x_j, y_i - y_j]^T
```

构造矩阵：

```text
A_i = sum_j p_ij p_ij^T W_ij
```

然后 MLS 权重为：

```text
W_j^MLS = [1, 0, 0] A_i^{-1} p_ij W_ij
```

最后界面流体粒子 `i` 的期望速度是：

```text
u_d(i) =
    sum_{inner fluid j} u*_j W_j^MLS
  + sum_{structure b} U_d_b W_b^MLS
```

其中：

- `u*_j` 是内层流体粒子的预测速度；
- `U_d_b` 是结构边界粒子的期望速度；
- 固定圆柱边界时，`U_d_b = 0`。

当前教学代码中对应位置：

- `MlsSource`: 存储 MLS 源点的位置、速度和原始权重
- `A[3][3]`: 二维 MLS 矩阵
- `invert3x3`: 求 `A^{-1}`
- `udx`, `udy`: MLS 插值得到的期望速度

## 5. 直接强制公式

论文 3.2 节的核心力公式是：

```text
f_i^{n+1/2} = rho0 * (u_d_i - u*_i) / dt
```

含义很直接：

```text
当前预测速度 u*_i  ----施加力---->  期望速度 u_d_i
```

如果目标界面点速度太大，就给一个反向力；如果速度方向不满足边界约束，就给一个修正力。

当前教学代码中写成：

```cpp
dfloat ftx = MLS_DIRECT_FORCE_FACTOR * points[i][j].rho *
             (udx - points[i][j].u[0]) / dt;
dfloat fty = MLS_DIRECT_FORCE_FACTOR * points[i][j].rho *
             (udy - points[i][j].u[1]) / dt;

points[i][j].Ft[0] += ftx;
points[i][j].Ft[1] += fty;
```

注意：当前 LBM 示例里 `u*` 暂时用的是 `computeMacroscopic()` 后的宏观速度。如果以后要更严格对齐论文，可以单独保存半步或预测速度。

## 6. BTDF 与 MLS direct forcing 的核心区别

| 对比项 | BTDF / 扩散型边界力 | 论文 MLS direct forcing |
|---|---|---|
| 力先在哪里计算 | 拉格朗日边界点 | 界面流体粒子 |
| 力如何进入流体 | 从边界点通过 delta 核扩散到欧拉点 | 直接加到界面流体点 |
| 边界点的角色 | 力源点 | 速度约束点、几何采样点 |
| 是否依赖 force spreading | 是 | 基本不依赖 |
| 是否容易跨薄壁两侧 | 较容易，取决于核支撑域和边界厚度 | 较少，因为只选一侧界面流体点 |
| MLS 的用途 | 可用于修正扩散核权重 | 用于插值界面流体点的期望速度 |
| 对复杂几何的关键难点 | 边界厚度、delta 核、拉氏点密度 | 正确判断界面点属于哪一侧 |

## 7. 当前代码中的 BTDF 路径

`Circle_IBM_BTDF()` 的逻辑可以简化为：

```text
for each Lagrange boundary point b:
    1. 从周围欧拉点插值得到边界点处的流体速度 u_b
    2. 和边界速度 U_b 比较，计算边界力 F_b
    3. 用 delta 核把 F_b 分配回周围欧拉网格点
```

当前代码中对应的关键片段：

```text
欧拉 -> 拉格朗日:
    uxt += u_corr_x * IB_Interp
    uyt += u_corr_y * IB_Interp

边界点力:
    fxt = 2.0 * rhot / dt * (uxt - upx)
    fyt = 2.0 * rhot / dt * (uyt - upy)

拉格朗日 -> 欧拉:
    points[xx_bc][yy_bc].Ft -= f_b * IB_Interp
```

这就是典型的“边界点算力，再扩散到流体”的路径。

## 8. 当前代码中的 MLSDirect 路径

`Circle_IBM_MLSDirect()` 的逻辑可以简化为：

```text
for each Euler grid point i:
    phi_i = signedDistance(i)

    if i is not outside-interface point:
        continue

    sources = []

    for nearby Euler grid point j:
        if j is inner fluid point:
            sources.push(j position, j velocity)

    for nearby Lagrange boundary point b:
        sources.push(b position, boundary velocity)

    build MLS matrix A from sources
    compute MLS weights
    interpolate desired velocity u_d at i
    compute direct force f_i = rho * (u_d - u_i*) / dt
    add f_i to points[i].Ft
```

关键区别是：拉格朗日点不再是“力源点”，而是“边界速度约束样本”。

当前 `fxl[p]` 仍然保留，是为了兼容原来的 Cd/Cl 输出。MLSDirect 中的 `fxl[p]` 是把界面流体所受力的反作用力归到最近边界点上，不是算法本身的 force spreading 步骤。

## 9. `MLS_INTERFACE_DISTANCE` 与 `MLS_SUPPORT_RADIUS`

这两个参数当前都设成 `2dx`，但职责不同。

```cpp
#define MLS_INTERFACE_DISTANCE (2.0 * dx)
#define MLS_SUPPORT_RADIUS (2.0 * dx)
```

含义如下：

| 参数 | 作用 | 影响 |
|---|---|---|
| `MLS_INTERFACE_DISTANCE` | 决定哪些流体点被认为是界面流体点 | 控制受力层厚度 |
| `MLS_SUPPORT_RADIUS` | 决定 MLS 插值时向周围找多远的源点 | 控制插值邻域大小 |

可以这样理解：

```text
边界 surface
|---- interface distance d ----|     被直接加力的流体点范围

目标界面点 i
|---------- support radius ----------|  用来找 inner fluid 和 structure 源点
```

它们可以相等，但不是同一个物理量。

## 10. 为什么论文方法对薄壁更友好

BTDF / DDF 的力从边界点扩散出去，核函数本身不知道“这是薄壁上侧还是下侧”。如果核半径跨过薄壁，就可能把两侧流体一起修改。

MLS direct forcing 则先判断：

```text
这个流体粒子是不是某一侧的 interface particle?
```

然后只对这些 interface particles 计算 `u_d` 和 `f_i`。这使得算法更容易限制在单侧流体层内。

但是这也带来新的要求：复杂几何里必须有可靠的几何查询。

对圆柱，符号距离很简单：

```text
phi = sqrt((x - xc)^2 + (y - yc)^2) - R
```

对复杂薄壁结构，则需要知道：

- 最近边界点是谁；
- 法向方向是什么；
- 当前流体点属于薄壁哪一侧；
- MLS 支撑域是否跨过了另一侧；
- 多个边界片段很近时，应该使用哪一个边界片段的速度约束。

所以 MLSDirect 不是自动解决所有复杂边界问题；它把难点从“边界力扩散厚度”转移到了“几何分类和同侧邻域选择”。

## 11. 学习时建议先抓住的三个变量

### 1. `u*`

这是没有完成边界修正前的预测速度。论文里它来自流体动量方程的预测步。

### 2. `u_d`

这是界面流体粒子应该达到的期望速度。它不是简单等于壁面速度，而是用内层流体速度和结构速度共同 MLS 插值得到。

靠近壁面时，`u_d` 更接近结构速度；远离壁面时，`u_d` 更接近内层流体预测速度。

### 3. `f_i`

这是直接施加在界面流体粒子上的力。

```text
f_i = rho * (u_d - u*) / dt
```

它的目的不是生成一个真实材料力模型，而是在数值上把界面流体粒子的速度推向无滑移约束所要求的速度。

## 12. 当前教学代码和论文的差异

当前 `Cylinder_IBM_testSPH.cpp` 是一个 LBM 欧拉网格示例，不是 SPH 粒子代码，所以做了几个教学近似：

1. 用欧拉网格点代替论文中的流体粒子。
2. 用圆柱 signed distance 做界面点判断。
3. 固定圆柱边界速度取 `(0, 0)`。
4. MLS 原始权重复用了当前代码已有的 `DELTA_FUNC`，而论文 SPH 语境中使用的是 SPH kernel。
5. `fxl` 仅用于兼容 Cd/Cl 输出，不表示 MLSDirect 仍在边界点扩散力。
6. 当前 `u*` 没有单独保存为论文中的完整预测速度变量，而是使用当前宏观速度近似。

这些差异不影响用它学习算法逻辑，但如果以后要严肃对比论文结果，需要逐项收紧。

## 13. 从教学代码迁移到 AMReX `InterpForce` 时的判断

如果目标是替换现有 AMReX case 中的 BTDF 型 `InterpForce(lev)`，最小迁移边界应当是：

1. 保留拉格朗日点作为几何和边界速度采样点。
2. 在欧拉网格或粒子容器中找界面流体点。
3. 对每个界面流体点收集同侧内层流体源点和边界源点。
4. 构造 MLS 矩阵并计算 `u_d`。
5. 在界面流体点上直接写入体积力。
6. 如果仍需要 Cd/Cl，单独把界面流体反作用力归约到结构表面。

最需要谨慎的不是 MLS 矩阵本身，而是第 2 和第 3 步：复杂边界下的同侧判断。

## 14. 一句话总结

BTDF 的思路是：

```text
边界点算力 -> delta 核扩散到流体
```

论文 MLS direct forcing 的思路是：

```text
界面流体点算期望速度 -> 直接在这个流体点上加力
```

MLS 的作用不是“扩散力”，而是让界面流体点的期望速度 `u_d` 在不规则粒子分布和薄壁附近仍然平滑、稳定。
