# BOX3D_OSI 算法原理与 AMReX 集成架构

本文是 `BOX3D_OSI` 的权威设计说明。后续实现若改变这里定义的状态不变量、phase
推进时机或 canonicalization 契约，必须同步修改本文。

## 1. 状态与边界

截至 2026-08-19：

- 已确定采用 one-step index（OSI）单数组方案；
- 已确定动态 AMR 布局变化时采用 canonicalize/rebuild/reset；
- `src/` 尚未包含 OSI 数据结构或 kernel；
- 当前可执行路径仍是 BOX3D 的 A-B 双 `MultiFab` 基线；
- 尚无 BOX3D_OSI 的编译、运行、数值或性能证据。

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

“valid/ghost”是逻辑坐标和所有权属性，不是底层存储槽的永久属性。对固定 `q` 和
`phase`，两者映射到互不重叠的存储槽；随着 phase 改变，同一原始槽可以先后承载不同
逻辑 cell。

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
进入 OsiAdvanceLevel(lev)：
    osi_phase[lev] = p
    storage 表示 t 时刻迁移后的 incoming DDF

1. Collision：
    在 A_q(x,p) 同址读取、碰撞、写回 post-collision DDF

2. Same-level MPI/ghost：
    从发送端 logical valid 的 A_q(x_send,p) 打包
    写入接收端 logical ghost 的 A_q(x_ghost,p)

3. phase commit：
    osi_phase[lev] = p + 1
    这一步在逻辑上完成 streaming

4. Physical boundary：
    按现有边界模型，在新 phase 的逻辑边界地址 A_q(x_b,p+1) 上修正 incoming DDF

离开 OsiAdvanceLevel(lev)：
    storage 再次表示迁移和物理边界处理后的 incoming DDF
```

phase 只能在整层本次碰撞、必要通信都完成后提交一次。不能让不同 Fab、不同 MPI rank
或同一 level 的不同 kernel 各自提前增加 phase。

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

这是设计选择，不是已验证结论。实现时必须用小网格排列测试确认：

- 任意 `q,p` 下所有 logical cell 映射唯一；
- 映射永不越出该 Fab 分配 Box；
- valid 和逻辑 ghost 在同一 phase 不发生别名；
- phase 跨越多个 Fab 长度周期后仍恢复原映射。

## 6. Ghost 与 MPI 的新契约

普通 A-B 布局直接把邻居值写入固定 `ghost(i,j,k,q)`。OSI 中，MPI 语义变为：

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
第一版需要显式 OSI-aware pack/unpack，或先 gather 到 canonical buffer 再调用 AMReX
通信。前者是最终性能方向，后者可作为正确性脚手架。

只发送真正跨面的方向可以减少通信量；发送全部 Q 个分量更容易建立初版正确性。两种
方案必须共用同一数值基线，不应在第一次实现中同时优化通信裁剪。

## 7. 物理边界

OSI 不改变 bounce-back、非平衡外推或当前 `fill_boundary()` 的物理公式，只改变 DDF
读写地址：

```text
普通：f(i,j,k,q)
OSI： f(A_q(i,j,k,phase),q)
```

当前 BOX3D 在显式 Stream 后对目标 `f_new` 施加物理边界。按第 4 节状态机，OSI 路径应
在 phase 提交后，对新 phase 的 incoming DDF 施加相同边界语义。

地址取模只是循环存储机制，不会把非周期物理边界自动变成周期边界。`Geometry`、边界
类型和 boundary work boxes 仍决定物理边界；OSI accessor 只决定写入哪个存储槽。

## 8. AMR level phase 与时间子循环

`JaberCycle2()` 中 fine level 每个 coarse step 推进两次，因此不能使用唯一全局 `step`
作为所有层的 OSI phase。至少需要：

```cpp
amrex::Vector<std::uint64_t> osi_phase;
```

每次 `OsiAdvanceLevel(lev)` 成功完成后只增加 `osi_phase[lev]`。在一个粗步内可能出现：

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

## 10. 动态 regrid：canonicalize/rebuild/reset

动态重构采用已经确定的第三种方案：把 regrid 视为 OSI phase 断点。

```text
所有相关 level 到达同步点
        |
        v
GatherCanonical：twisted -> canonical old layout
        |
        v
必要的完整 AverageDownValid
        |
        v
RefineMesh / RemakeDdfState
旧 fine valid 迁移 + coarse 初始化新增 valid
        |
        v
得到 canonical new layout
        |
        v
建立新的 twisted state，target_phase = 0
        |
        v
重建与 BoxArray/DM 相关的 cache
        |
        v
osi_phase[lev] = 0
```

重置 phase 不会改变物理解。若重构前有：

$$
M_{old}[A_{old}(x,q,p)]=f_q(x),
$$

gather 后 `C(x,q)=f_q(x)`；新布局以 phase 0 写入时
`A_new(x,q,0)=x`，所以新状态仍表示同一个 `f_q(x)`。

第一版优先保证 regrid 前后 valid DDF 等价，不追求避免 canonical 临时缓冲。性能稳定后
再评估只规范化受影响 patch 或使 `RemakeDdfState()` 原生理解 twisted layout。

## 11. 静态 coarse-fine 传输

即使 BoxArray 不变，细层 ghost 填充和 fine-to-coarse restriction 也跨越两个具有不同
phase 的 level。第一版正确性路径可以使用 canonical staging：

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

这会增加内存和搬运，不代表最终性能方案。它的用途是先隔离两个问题：

1. OSI 单层推进是否数值正确；
2. AMR coarse/fine 数值转换是否在 twisted/canonical 边界上正确。

只有 correctness 路径通过后，才应将 direct interpolation cache、fused restriction 等
逐步改造成 OSI-aware kernel。

## 12. Checkpoint、输出与 restart

推荐 checkpoint 持久化 canonical DDF，而不是依赖旧 Fab 的 twisted 地址。这样重启时
可以按新/旧相同的 BoxArray 读入，并统一建立 phase 0 的 OSI 状态。

checkpoint header 至少应增加：

- DDF layout/version 标记；
- 写出的是 canonical 还是 twisted；
- 若允许 twisted checkpoint，则必须保存每层 phase 和完整映射版本。

第一版只支持 canonical checkpoint。plotfile、宏观量计算和 DDF reference comparison 也
应从 canonical view 或 OSI-aware accessor 读取，不能直接解释 raw twisted MultiFab。

## 13. 建议的数据结构边界

以下是实现框架，不代表已有接口：

```cpp
enum class StreamMode : int {
    AB = 0,
    OSI = 1
};

struct OsiLevelState {
    amrex::MultiFab ddf;
    std::uint64_t phase = 0;
};

struct OsiFabLayout {
    amrex::Dim3 lo;
    amrex::Dim3 len;
};
```

职责划分建议：

| 位置 | 职责 |
| --- | --- |
| `AmrCoreLBM.H` | stream mode、每层 phase、OSI state 和 canonical adapter 声明 |
| `OsiIndex.H` | 非负取模和无状态 Fab-local host/device 地址映射 |
| `Kernels.H` | fused OSI collision kernel 和 boundary accessor |
| `AmrCoreLBM.cpp` | per-Fab launch、MPI pack/unpack、canonical gather/scatter、regrid/checkpoint 生命周期 |
| `main.cpp` | 根据 stream mode 选择 A-B 或 OSI 推进；保持 AMR 调度一致 |
| `config/inputs` | `lbm.stream_mode`，默认 0 直到验证完成 |

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

1. **phase off-by-one**：通信用 `p`、边界用 `p+1` 的状态定义若混乱，会产生看似稳定但方向错位的结果。
2. **直接调用 AMReX copy**：twisted MultiFab 传入 `FillBoundary/ParallelCopy` 会按错误逻辑坐标复制。
3. **Fab 维度变化**：重构后沿用旧 `smallEnd/length` 会越界或静默读错。
4. **层间 phase 不同**：用 coarse step 代替 per-level phase 会在子循环后错位。
5. **raw ghost 清理**：按固定存储下标清 ghost 可能清掉当前映射下的 logical valid。
6. **restart 丢失布局语义**：只保存 raw DDF 而不保存/消除 phase，重启后无法解释。
7. **过早删除 A-B 基线**：没有同输入对照时无法确认错误来自 OSI、AMR 还是物理模型。
8. **把 canonicalization 成本隐藏**：性能统计必须单列 gather/scatter/regrid reset，不能归并后宣称 OSI kernel 提速。

## 16. 设计完成的判据

在开始大规模实现前，代码评审至少应确认：

- phase 状态机只有一个解释；
- Fab 循环 Box 的范围已确定；
- 同层 MPI 的 send/receive logical region 已明确；
- 物理边界在哪个 phase 施加已明确；
- coarse/fine 各自的 phase 如何进入传输适配器已明确；
- regrid 和 restart 均有 canonicalization 入口；
- A-B 路径仍可在同一源码树中运行；
- 测试能覆盖地址置换、单层数值、MPI、AMR、regrid 和 restart。

具体实施顺序和验收矩阵见
[OSI 实施与验证计划](osi_implementation_plan.md)。
