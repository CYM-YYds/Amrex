# AB/OSI 传输融合知识收尾

目的：使两轮融合后的当前机制、冻结验收记录和交接入口一致。范围仅 BOX3D_OSI 文档；不改求解器、输入、测试或运行流程。

## 当前事实与修正

- 已提交实现基线为 `a6bd3db`，第一轮 `96aa0cb` 是历史阶段。README 保留入口摘要，架构文档为机制说明，current_status 为验收边界入口。
- 改正“完整 valid 平均和重网格插值保持原实现”的现役歧义，并将碰撞候选记录中的“当前5处未提交修改”限定为当次工作区描述。
- 架构补全完整 valid 局部结果区，明确持久层缓存与局部初始化/完整平均任务的生命周期；保留 AB 对 `f_new` 的辅助缩放写入差异。
- 历史测量与变更记录不改写。旧作业的程序指纹、性能和数值范围不自动代表当前工作区。

## 事实面与证据

| 事实面 | 状态 | 本次核对 |
|---|---|---|
| 代码 | verified-current（已提交基线） | 核对共享建表、批量执行器、辅助写入及局部同步调用；审计期间出现的 `src/AmrCoreLBM_advance.cpp` 额外格式修改保留、不提交，不声称工作区与冻结源码逐字节一致。 |
| 运行态 | verified-current（冻结验收范围） | 只读查询 620242/620330/620331 为 SUCCEEDED，620246 为 FAILED；核对最新两份 JSON 报告及程序 SHA256。正式 DIM=2/3 程序均等于候选副本，7项基线/候选可执行文件校验全部通过。 |
| 文档 | changed-and-verified | 修正 README、架构及交接状态；检查本次 Markdown 本地链接和 diff。 |
| 规则 | verified-current | 根 `AGENTS.md -> CLAUDE.md` 软链有效，适用规则及路径未发现本次相关冲突；不新增平行规则。 |
| 记忆 | out-of-scope（generated-read-only） | 只读检索旧索引，用作审计线索；当前实现以源码和上述文档为准，未写生成记忆或 correction note。 |
| 工作区 | verified-current（只读盘点） | 枚举算例140份 Markdown，按本次传输改动路由检查；仅一个 worktree，额外源码格式修改未纳入本次提交。未删除材料。 |

620330 报告 `numerical_passed=true`、`valid_all_finite=true`、`strict_full_valid_passed=true`；
620331 仅分区等价通过，后两项为 false。620246 的 FAILED 来自严格分析拒绝既有 covered 非有限值，不能改写为调度成功。
本次只核对已完成证据，不重跑数值、编译或性能实验；完整范围见
[进一步融合验收](2026-10-26-ab-osi-transfer-complete.md)与[第一轮验收](2026-10-26-ab-osi-transfer-fusion.md)。

## 保留与未决范围

- 保留 `runs/temporary/ab_osi_transfer_fusion/` 和 `runs/temporary/ab_osi_transfer_complete/` 的输入、源码快照、程序、命令、日志、checkpoint、指纹与报告。
- 保留 `scripts/temporary/` 中四个 transfer 脚本：提交、分析、GPU测试源码及构建入口，仍是两轮结果的复现依赖。两轮目录均已标注 AI 短测用途；不新增重复入口。
- 既有 `scripts/temporary/__pycache__` 与 `tests/__pycache__` 未判定为本次产生，不列为已授权删除物。没有本次新增的待删分支、worktree 或可确认无依赖的实验中间产物；未执行清场。
- OSI covered 非有限值的根因/修复仍 pending；两 GPU、跨节点、生产128³及真实加密判据下的本轮性能仍未验收。独立碰撞实验619072的旧 pending 不由新的不同 checkpoint 核对消除。
- 其他算例、结构审查、历史目录归并、推送/PR/部署不在本次范围。
