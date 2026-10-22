# 平均映射合入后的知识收尾

## 范围与现役入口

按neat-freak完整路径核对BOX3D_OSI及直接上级规则，基线为7c4a034。
未审计或修改其他算例、全局配置、远端发布；不进行求解器结构审查。
用户入口以README为准，机制以OSI架构为准，当前验收与历史边界以current_status为准。

| 事实面 | 状态 | 本次证据与处置 |
|---|---|---|
| 代码 | verified-current | Q27半warp16/父单元、Q19单线程/父单元，均128线程/block；Q9保持256线程/整warp。编译期选择、默认Q27；无新运行时开关。源码与冻结验收副本一致。 |
| 运行态 | verified-current | 重新执行合入目录audit.py，核对52次成功运行、19组短程/生命周期对照、2组生产512步对照及正式二/三维程序SHA256。这里只复核已保存凭证，不表示本轮重跑或有模拟正在运行。 |
| 文档 | changed-and-verified | 修正README旧整warp/Q19仅候选说法及失效标题链接；MLUPS历史页补现役指针；3篇直接相关旧实验报告注明冻结版本和后续采纳，不改写原始数字。旧实验README说明旧audit的时点约束。 |
| 规则 | verified-current | 根AGENTS.md仍软链CLAUDE.md；作用域内无新增override或算例规则。规则、README路径存在，源码/物理配置未改，不新增规则或skill。 |
| 记忆 | pending | generated-read-only。生成记忆中“生产仍整warp”的旧结论已过期，当前以源码/现役文档为准；用户未明确要求更新记忆，本轮不修改生成文件或写correction input。 |
| 工作区 | verified-current | 收尾前工作树干净、detached HEAD、仅一个worktree；本轮仅改文档。无本次遗留提交/准备脚本，无删除/迁移/停作业操作。 |

## 核验与保留

只读盘点机械列出123个Markdown文件，清单保存在
[runs/average_mapping_merge_20261022/neat_inventory_20261022.txt](../../runs/average_mapping_merge_20261022/neat_inventory_20261022.txt)。
重点全文核对README、现役架构/交接、合入报告及直接相关历史结果，不将全部历史文档当作现役声明。
[验收汇总](../../runs/average_mapping_merge_20261022/audit.json)确认源码、inputs及正式程序与测试副本一致。
本轮为文档修改，仅检查相关本地链接/路径与git diff，不新增数值测试或重建。

旧average_matrix目录audit.py包含“实验时正式文件未变”检查，现役源码已合入后该项预期不通过；
不能为了令旧审计通过降低门槛或覆盖旧指纹。当前使用average_mapping_merge目录audit.py，
旧性能/数值表仍可用各冻结summarize入口重算。

两个既有scripts/temporary续跑内存工具及tests/__pycache__不属于本次平均映射工作，来源/后续依赖不在本轮裁决，保留原样。
实验源码、程序、编译/运行日志、checkpoint、分析工具及失败构建记录都是复核材料，不列为删除候选。
没有需要用户确认的清场动作；复核现场完整保留。

## 未闭合边界

- pending：生成记忆中的旧结论未获明确写入授权，保持只读；不能声称记忆已同步。
- out-of-scope：其他算例、旧续跑任务、远端推送/发布、长期物理收敛与新增多GPU性能验收。
- warning：原基线covered非有限模式和历史physical-ghost差异未由本轮修复；具体数值范围沿用[生产合入记录](2026-10-22-average-mapping-production-merge.md)。
