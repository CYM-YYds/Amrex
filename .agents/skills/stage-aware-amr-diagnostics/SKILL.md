---
name: stage-aware-amr-diagnostics
description: 调试 AMR-LBM 的 A-B/OSI 数值差异时，按重构、插值、碰撞、通信、迁移、边界和平均阶段选择实际涉及的网格范围并输出可验证证据。
metadata:
  short-description: 按计算阶段选择 AMR 一致性检查范围
---

# 按阶段选择 AMR 诊断范围

用于 BOX3D/BOX3D_OSI 或相同数据流的 AMR-LBM 一致性排查。核心规则是：**检查范围跟随当前阶段的读写集合改变，不能用一个“全体网格”比较器代替所有阶段。**

## 两阶段工作法

### 1. 尚未锁定首个差异时间步

- 按固定间隔（通常每 32 步）锁步推进 A-B 与 OSI。
- 所有层级只先比较 `uncovered valid` 区域，按 `level、logical cell、q、phase、stage` 记录首个差异。
- 同时记录每层 `finest_level`、Box 数量、valid cell 数、covered/interface 计数和非有限值。
- 这个阶段的结论只能是“首个 uncovered 差异在某步某层某阶段出现”，不能据此断言所有 ghost、covered 或 interface 都一致。

### 2. 已锁定时间步后

把检查目标切换到该时间步，在同一时间步内按生命周期分段检查。每段必须写明 `step、lev、phase、stage、范围定义`。

## 各阶段的检查范围

| 阶段 | 必须检查的范围 | 必须排除或单独分类的范围 |
|---|---|---|
| 网格重构 | 所有 active level 的所有 valid cell；包含 coarse covered 和 interface；检查 BoxArray、DistributionMap、covered/interface mask、有效 DDF | 不把“Box 数量一致”当作 DDF 一致；物理域外 ghost 单独检查 |
| 重构后首次状态/插值 | 实际被插值写入的 fine ghost、coarse/fine stencil source、对应 level 的 phase-aware raw 地址 | 物理边界 ghost 与粗细接口 ghost 分开统计；不要只比较 uncovered valid |
| 碰撞 | 与 `Collide(lev,n,layout)` 相同的 launch box、active mask、interface 例外规则；逐 cell、逐 q 比较碰撞前后 | 不比较未被碰撞 kernel 访问的整个 Fab；covered interior 若被跳过必须明确标记 |
| 通信 | 实际 FB/CPC/OSI tag 的 send、receive 和目标 ghost 区；包括同层 patch ghost、MPI ghost、周期 ghost | 非周期物理边界 ghost 不归入通信正确性，另列为 physical-boundary ghost |
| 迁移/Stream | 实际 target launch cell；每个 q 的 source 是 `target - e[q]`；检查 source 的 valid、interface/covered、内部 patch ghost | 迁移比较先排除非周期物理边界 target；物理边界 target 由 Boundary 阶段处理 |
| 边界处理 | `boundary_work_boxes` 覆盖的物理边界 valid/ghost 和所有 active uncovered 边界 cell；比较 Boundary 后结果 | 不把内部 interface 或内部 patch ghost 归入物理边界处理 |
| 平均/Restriction | 实际 fine-to-coarse source、coarse destination、covered/interface 过渡区和 average-down cache 覆盖范围 | 不用全层 norm 替代逐点 source/destination 检查；重构后重新暴露的 coarse cell 单独记录 |

## 分类定义

- **physical-boundary ghost**：source 坐标超出 `Geom(lev).Domain()`，由非周期物理边界条件产生或更新。
- **internal patch ghost**：source 仍在物理域内，但不属于当前 Fab 的 valid box，由同层 patch/OSI 通信提供。
- **covered/interface**：coarse cell 被 finer level 覆盖；interface 是 covered 区域贴近 uncovered 区域的过渡带。当前实现的 `interface_mask` 主要标记 covered 一侧，报告时必须说明这一语义。
- **uncovered valid**：当前层级真正参与本阶段 active 计算的 valid cell。

## 当前 BOX3D_OSI 的实现注意事项

- `CompareOsiReferenceStage` 默认会跳过 `covered` coarse cell。只有需要“重构后所有 valid DDF”时，才显式关闭 covered 跳过；报告中必须写明是否包含 covered。
- `compare_ngrow > 0` 只表示比较器申请了 ghost 范围，不等于范数一定覆盖所有 ghost；需要对实际 source 坐标逐点检查并统计 valid/physical ghost/internal ghost/interface。
- `AfterRefineMesh` 的网格诊断应同时给出 topology、mask 和逐 cell/逐 q DDF 结果；`REGRID_DIAG` 或 `CF_MASK_DIAG` 单独不能证明数值一致。
- OSI 值必须用当前 `phase` 和当前 Fab ring 的 `osi_address()` 解码后再与 canonical A-B 值比较。主机侧诊断使用显式 host velocity table，不直接调用设备端 `e[q]`。
- 对迁移阶段，先检查 source 是否已经不一致，再解释 Stream 后 target 差异。Stream 后首次出现差异不证明 Stream 产生了根因。

## 推荐的锁步输出

每个 level 至少输出以下阶段标记：

```text
Initial
InterpolationGhost
Collision
Communication
CommunicationGhost
AfterCommunicationSources
Stream
BoundaryUncovered
Swap
```

锁定时间步后，重构生命周期还应输出：

```text
AfterRefineMeshAllValid
AfterFirstFillGhost
AfterAverageDown
AfterRepair
```

每条失败记录至少包含：`step、lev、phase、stage、logical target、q、velocity、source logical、raw address、physical_boundary、source category、AB value、OSI value、L_inf`。

## 结论门槛

- “网格一致”必须拆成 topology 一致、mask 一致、valid DDF 一致、ghost 一致、interface/source 一致，不能合并成一句。
- 只有在对应阶段的实际读写范围全部逐点通过后，才能声称该阶段一致。
- 编译成功、作业成功、全局 checksum 或 uncovered profile 稳定，都不能替代阶段范围内的逐点证据。
- 若同一阶段同时存在 physical-boundary ghost 和 internal/interface mismatch，先分开报告，不能用物理边界解释内部 interface 差异。
