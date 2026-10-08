# 三层网格接口重合时的插值与 ghost 修复方案

状态：设计草案，供修改与确认；尚未实现、编译或运行验收。

日期：2026-10-08。源码核对基线：`74ec947`。

## 1. 目标与范围

支持 level 0、1、2 三层中，L1 与 L2 的 valid 并集在物理空间覆盖相同区域、
域内粗细接口重合的布局。网格索引按各层分辨率解释，相同物理范围不表示索引相同。

第一阶段限于 BOX3D_OSI、ratio=2、现有无外力 BGK 路径。A-B、OSI direct 和
canonical fallback 使用相同的模板来源规则。先保留 `Cycle2` 的递归推进和
接口平均时机；若分析或逐值验证表明必须改变调度，再单独修订本方案。

正常 proper nesting 布局应继续使用父层 valid 来源。只重合 Fab 分界、外围仍有
父层 valid 覆盖的情况，与整个层的域内接口重合分开处理。物理边界、周期覆盖也
分别检查，不能把它们统一归为“父层 ghost 缺口”。

本方案不把旧 `FillPatchTwoLevels` 调用视为重合布局正确性的依据；普通两层填充
也依赖父层模板来源充分。多层递归补缺可作为后续替代设计，但需处理 LBM 缩放和时间状态。

## 2. 当前源码事实与问题

| 位置 | 当前行为 | 重合布局下的风险 |
| --- | --- | --- |
| [BuildDirectInterpolationCache](../src/AmrCoreLBM_amr.cpp) | fine ghost 经 `DdfInterpolater()->BoxCoarsener()` 生成 sparse coarse staging；CPC 的源 ghost 宽度为零 | 模板可能超出父层 valid 并集 |
| [FillDdfGhostFromCoarse](../src/AmrCoreLBM_amr.cpp) | `ParallelCopy` 仅复制 coarse valid，然后边界延拓、缩放、插值 | 域内缺口没有来源 |
| [FillOsiGhostFromCoarse](../src/AmrCoreLBM_osi.cpp) | direct CPC 只取 valid；fallback 用 `DecodeOsiValid()`，再以源 ghost 宽度零复制 | 放开其中一条分支不能修复全部路径 |
| [Cycle2](../src/main.cpp) | 填子层 ghost、推进本层、递归子层两次、平均一次 | 第二次子步插值依赖父层推进后的 ghost |
| [Stream / SwapLevel](../src/AmrCoreLBM_advance.cpp) | A-B 迁移更新 `growntilebox(n-1)` 后交换；OSI 递增 phase | 数组中存在 ghost 地址，不表示该地址对应的逻辑 ghost 已可靠更新 |
| [AverageDownOsiLevel](../src/AmrCoreLBM_osi.cpp) | 2×2×2 平均结果写入父层接口 valid | 同层 ghost 副本没有随这次回写同步 |

这些是静态源码事实和由此推导的风险，尚未构造重合布局捕获实际首差。
不能据此认定历史正常嵌套运行已经受影响。

当前 `nghost=2`、ratio=2。以一维截面说明：父层 valid 左端为 `a`，细层左端为
`2a`。细层 ghost `2a-2` 的三线性插值会读取父层 `a-1` 和 `a-2`，因此需要
父层外侧两圈。守恒线性与单元二次模式的实际模板也需分别检查。

父层第一次 A-B 推进以 `n=2` 执行时，迁移只更新内侧一圈 ghost；第二次向子层
插值若仍读取外侧第二圈，就没有可靠的当前状态。这与“复制是否允许读 ghost”是
两个独立问题。OSI 也必须通过 pull source 和 phase 核对同一有效范围。

## 3. 当前推进时序与时间约束

```text
Cycle2(L0, t)
  L0 → L1：填 L1 ghost
  L0：推进至 t + dt0

  Cycle2(L1, t)
    L1 → L2：填 L2 ghost
    L1：推进至 t + dt1
    L2：连续两步
    L2 → L1：接口平均

  Cycle2(L1, t + dt1)
    L1 → L2：再次填 L2 ghost
    L1：推进至 t + 2*dt1
    L2：连续两步
    L2 → L1：接口平均

  L1 → L0：接口平均
```

`dt1=dt0/2`。当前两条 `Fill*GhostFromCoarse()` 都将 `time` 标为未使用，
并没有借助传入时间自动选择粗层历史状态或做时间插值。

建议让 L1 的外部 ghost 从起始填充值随 L1 子步演化，供第二次 L2 插值使用。
不能在第二次 L1 子步入口直接用已推进到 `t+dt0` 的 L0 当前态覆盖它们，
然后把结果当作 `t+dt1` 的数据。

扩大 ghost 后仍需证明其演化、边界和接口 valid 的状态在读取时一致；上面的时间标注
是调度意图，不是数值时间精度已经得到证明。

## 4. 拟采用的模板来源规则

### 4.1 每个目标模板按以下顺序建立来源

1. 优先匹配父层 valid 并集和周期像，保留现有 valid 复制路径。
2. 只对剩余的域内缺口，匹配已准备且当前有效的父层粗细 ghost。
3. 一个缺口单元选定唯一源 Fab、周期位移和 owner rank，避免重复写入。
4. 非周期域外模板继续使用现有物理边界延拓；它的域内来源也必须有效。
5. 缺口仍没有来源时明确报错，输出层号、fine work box、coarse stencil 和缺失 box。

静态缓存保存空间来源及最大所需宽度；运行时检查当前子步的有效范围。
有分配空间但当前无效的 ghost 不得作为复制来源。不同副本若不一致，应在诊断中报告，
不能用“固定选一个副本”掩盖有效性问题。

### 4.2 建议增加的缓存信息

| 信息 | 用途 |
| --- | --- |
| valid 来源的复制 tags | 复用当前快速路径 |
| ghost 缺口的复制 tags | 记录 source/destination box、Fab 索引、周期位移和 MPI 归属 |
| 每层模板需求范围 | 推导父层需准备的有效 ghost 宽度 |
| 每个子步的有效范围或宽度 | 拒绝读取已消耗的外圈 |

具体成员名和数据结构待实现时确定。缓存必须在 regrid、DistributionMapping 改变、
restart 或 state 重新分配后重建；不得缓存临时 canonical 数组的失效地址。

### 4.3 A-B 与 OSI 接入

| 路径 | 改动要求 |
| --- | --- |
| A-B | coarse staging 先复制 valid，再按缺口 tags 复制有效 coarse ghost |
| OSI 单 rank direct | 按 ghost 来源 Fab 的完整存储几何和当前 phase 解码到 staging |
| OSI 多 rank direct | local tags 和 MPI pack 使用相同来源规则；目标仍为 canonical staging |
| OSI fallback | 增加所需 ghost 区域解码，不能只调用 `DecodeOsiValid()` |

staging 完整后，再执行现有物理边界延拓、当前粗细层缩放与插值。
父层原始状态保持父层尺度；L0→L1 已缩放的 ghost 在 L1→L2 时只施加该相邻层
所需的缩放，不重复施加 L0→L1 缩放。valid/ghost 两类来源使用同一缩放规则。

不能把所有 CPC 的 `IntVect(0)` 机械替换为 `nghost`：需区分源宽度、目标宽度、
有效来源、重叠优先级以及 raw/canonical 数据布局。

## 5. ghost 宽度与推进范围

将目前共用的宽度概念拆开：

| 概念 | 含义 |
| --- | --- |
| 分配宽度 | state/Fab 实际存储多少圈，OSI 地址几何由它决定 |
| 填充宽度 | 本次从父层准备多少圈可靠数据 |
| 当前有效宽度 | 当前子步实际可安全读取多少圈 |
| 碰撞范围 | 本步对哪些有效单元执行碰撞 |
| 迁移范围 | pull source 充分时，本步能更新哪些目标单元 |

对域内 D3Q27 pull streaming，若有效来源宽度为 `w`，一次迁移后可保证的
外部目标宽度通常为 `w-1`；这个保证要求所需碰撞源、同层通信和边界处理完整。
多 Fab、周期、非周期边界分别验证，不能只用宽度公式代替源地址检查。

三层重合、子层填两圈的候选安排：

| L1 阶段 | 候选有效宽度 | 用途 |
| --- | --- | --- |
| L0 首次填 L1 后 | 3 | 为两次 L1 子步准备外部状态 |
| 第一次 L1 推进后 | 2 | 第二次 L1→L2 插值可读取两圈 |
| 第二次 L1 推进后 | 1 | 不再用它执行本轮 L1→L2 插值；下一轮重新准备 |

“三圈”是上述几何与时序的候选需求，不是通用层数的固定值，也不是已验证结论。
应从最细层向上，使用实际 `BoxCoarsener` 推导子层填充所需的父层范围，再计入
该范围在下一次读取前经历的迁移次数。更深层、改变插值模式或推进调度时重新推导。

实现时审查 `Stream()` 的 `n-1<=1` 限制、grown box 发射范围、covered/interface
mask 宽度、同层通信范围以及物理边界 work boxes。covered valid 即使位于 Fab
范围内，也必须在当前阶段有可靠值；拓扑 covered 标记不能替代数值有效性。

尤其对 OSI，扩大 state 分配会改变各 Fab 的 phase 地址几何，所有读写、通信、
边界、平均、检查点和诊断必须使用实际分配几何。无效外圈的 phase 地址回绕不能
被误认为有效的物理迁移。

## 6. “平均后刷新 ghost”的准确含义

L2→L1 平均写回 L1 接口 valid，不会同步写回其他 L1 Fab 的同层 ghost 副本。
如果后续插值从那些副本读取，可能得到平均前的数据。

本方案用 valid 优先的来源规则规避这一读取：有 L1 valid 来源的位置直接读取
拥有该 valid 的 Fab，跨 rank 时直接由其 owner 发送。只在整个 valid 并集外读取
粗细 ghost。因此暂不增加无条件的“每次平均后全层通信”。

后续其他计算若确实在现有同步点之前读取同层 ghost，仍需在它的读取前同步，
或改为直接读取有效来源。这应由实际调用链和源地址诊断决定。

整个 L1 valid 并集外的粗细 ghost 无法通过同层 FillBoundary 补齐；它们需要此前
准备并随 L1 演化的状态。重新执行 L0→L1 填充属于不同操作，必须检查时间状态。

2×2×2 平均核和当前接口平均时机先保留；确认重合布局下 L2→L1→L0 需要的
边缘 valid 都被可靠更新。若逐值证据发现范围或调度缺口，再扩大平均修改范围。

## 7. 新建层、regrid 与 restart

运行时 ghost 插值修复后，仍需覆盖下面的入口：

- `InitializeNewLevelFromCoarse()` / `InterpolateCanonicalToFine()`：新 fine valid
  贴着父层边缘时，完整模板也可能需要父层 ghost。
- `RemakeDdfState()` 与 OSI 重构插值路径：旧 fine valid 优先迁移；新增区域按统一
  模板规则补齐。不能假定 regrid 回调期间旧缓存与新布局一致。
- 初始化回调：第一次 coarse/fine 缓存尚未建立时，需有独立的来源准备办法。
- restart：按检查点恢复实际层级和布局，重新推导宽度、构造 state 地址几何与缓存。

现有 regrid 前完整平均和物理边界修复应纳入来源有效性分析，不自动扩大其范围。

## 8. 文件改动清单

| 文件 | 计划职责 |
| --- | --- |
| [AmrCoreLBM.H](../src/AmrCoreLBM.H) | 缓存信息、分配/填充/有效宽度接口 |
| [AmrCoreLBM.cpp](../src/AmrCoreLBM.cpp) | 新状态的初始化与容器管理 |
| [AmrCoreLBM_amr.cpp](../src/AmrCoreLBM_amr.cpp) | 来源分类、缓存构造、A-B staging、新建层与重构支持 |
| [AmrCoreLBM_osi.cpp](../src/AmrCoreLBM_osi.cpp) | ghost raw 解码、local/MPI staging、fallback 和地址几何 |
| [AmrCoreLBM_advance.cpp](../src/AmrCoreLBM_advance.cpp) | 碰撞/迁移范围和有效范围更新 |
| [main.cpp](../src/main.cpp) | 必要的子步状态传递；先保持现有递归和平均顺序 |
| [OsiCommunication.H](../src/OsiCommunication.H) | 仅在现有 tag/transport 无法表达 ghost 来源时扩展 |
| [AmrCoreLBM_diagnostics.cpp](../src/AmrCoreLBM_diagnostics.cpp) | 模板覆盖、逐值及首差诊断，集中在可关闭入口 |

实际涉及的初始化、检查点和验证文件在实现时补充。生产 `config/inputs` 暂不改变；
测试使用独立输入快照。不能通过降低 `amr.n_proper` 直接推断任意重合布局已获支持。

## 9. 实施顺序与验收

1. 构造受控三层域内重合布局，记录几何、输入和 executable/source 指纹。
   明确固定 BoxArray 的测试入口；默认 proper nesting 可能阻止它出现，不能只改阈值
   然后假定已有重合。测试期间不要覆盖历史 checkpoint 或生产配置。
2. 先验证模板缺口和第二个子步的实际 source；输出 target/source/q、Fab、rank、
   来源类别、子步、phase/raw 地址、所需与实际有效宽度。
3. 实现共享的来源和宽度规则，再接入 A-B、OSI 单 rank direct、fallback、多 rank。
4. 扩展新建层、重构、restart；完成对应阶段检查。
5. 通过数值验收后再测正常嵌套路径的开销，保留 valid-only 快速路径。

验收矩阵：

| 项目 | 要求 |
| --- | --- |
| 模板覆盖 | 每次插值所有必读位置都有唯一有效来源；无未填充 staging 读取 |
| 独立插值验证 | 非均匀、可计算预期值的 DDF 场；检查各模式适用的常量/线性再现、缩放和有效 child 平均性质 |
| 两个子步 | 分别检查第一次和第二次 L1→L2 填充，证明第二次未读取失效外圈 |
| 阶段逐值 | 每步、每层比较 A-B/OSI 的所有 uncovered valid，检查接口写回和实际 ghost pull source |
| 范围分类 | uncovered、interface、covered interior、同层 ghost、粗细 ghost、物理 ghost 分开报告 |
| 分解与边界 | 单 Fab、多 Fab、跨 rank；非周期、周期及混合边界分别运行 |
| 生命周期 | 固定布局、动态 regrid、restart；确认缓存与新几何/phase 匹配 |
| 正常布局回归 | 原正常嵌套条件下逐值回归，并记录额外内存、通信和阶段耗时 |

逐值误差门限在测试前固定并记录，可从现有 `1e-12` 门限开始评估；不能根据结果事后放宽。
A-B/OSI 一致不能排除共享算法错误，必须结合独立模板验证和 ghost 有效性证据。
编译完成、运行结束、有限值或 aggregate checksum 一致都不能代替这些验收。

## 10. 供修改与确认

- [ ] 范围：先支持三层 ratio=2、现有无外力 BGK；其他层数和模型另行验证。
- [ ] 模板来源：valid 优先，只用有效粗细 ghost 补域内缺口。
- [ ] ghost 宽度：接受按实际模板和子步消耗推导，不固定全层三圈。
- [ ] 时间策略：优先扩大并演化父层 ghost；不直接用提前推进的祖父层当前态重填。
- [ ] 平均策略：先保留核函数和时机，不无条件添加平均后的全层通信。
- [ ] 生命周期：实现范围包含新建层、regrid 和 restart，不只修普通推进。
- [ ] 验收：包含独立模板测试、第二子步源检查和逐层逐值回归。

修改意见：

> 在此填写范围、实现偏好、待解决问题和验收门限。

确认记录：

> 在此填写确认日期及接受的方案版本。当前复选框不代表已确认或已实现。
