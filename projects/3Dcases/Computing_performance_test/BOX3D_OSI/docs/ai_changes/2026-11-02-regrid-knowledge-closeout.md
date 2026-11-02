# 重构性能知识收尾（2026-11-02）

目的：消除现役架构、运行入口与已完成实验之间的过期说法。本轮仅同步 BOX3D_OSI 文档，不改源码、配置、冻结实验或用户已有修改。

## 事实面与处置

| 事实面 | 状态 | 权威证据与处置 |
|---|---|---|
| 代码 | verified-current | 源码基线 `5373085`；三维 FLOAT 程序与 622773 的 candidate 一致。现役 state_error_3、按层失效缓存、验证目标步门控已核对，机制统一在[架构说明](../osi_algorithm_and_architecture.md)。 |
| 运行态 | verified-current | 调度器复核 622773/622351/622348/622345/622316/622137 为 SUCCEEDED；622335/622314/622116 为 FAILED，应用退出与独立分析通过不等于整作业成功。无服务部署，部署面 not-applicable。 |
| 文档 | changed-and-verified | README、当前状态、架构、性能索引和生产运行父目录更新。完整成本数字只在[定量报告](2026-11-02-regrid-cost-quantification.md)维护，历史报告保留自己的冻结范围。 |
| 规则 | verified-current | 根 AGENTS.md 正确软链至 CLAUDE.md；核对生效规则链、直接引用路径，不新增并行规则或重复工作流。 |
| 记忆 | out-of-scope（generated-read-only） | 只读历史索引用于定位；旧约 0.72% 及已失效文档指针不能充当现役结论。本次未获直接写记忆授权，未改生成记忆。 |
| 工作区 | changed-and-verified | 用户修改与科学复现材料保留；预览后获用户明确授权，275 个意外检查点已删除，见文末清场复核。 |

## 关键边界

- 622137/B 正常完成 128000 步、t=1000，但 converged=0；其冻结程序早于本轮重构变更，不能作为当前 HEAD 长程验收。
- 622116 虽完成推进至 t=1000，最终 VisMF 输出失败、exit 6，没有完整终点场，不能当作成功场对照。
- 622335 的计划数值对照独立核验成立，但作业因意外额外长测被终止；622314 的应用运行与检查点相符不抹去汇总器失败。详情沿用各自[检查职责记录](2026-11-01-regrid-verification-boundary.md)和[缓存记录](2026-11-01-regrid-cache-reuse.md)。
- 二维 Q9 restriction 分组的既有潜在重复写风险仍在架构文档中，数值正确性 pending；本轮未重新构建或验收二维、DOUBLE、多 GPU，也未证明物理收敛。其他算例、全仓结构审查及全局配置 out-of-scope。

## 清场预览与保留范围

盘点前机械枚举本算例 253 篇 Markdown；只深入审计本次命中的入口、机制和实验记录，不宣称逐篇内容审完。清单、初始状态、用户 diff 和精确删除候选保留在
[runs/temporary/regrid_knowledge_closeout/round1](../../runs/temporary/regrid_knowledge_closeout/round1/)。

唯一大体积删除候选是 `regrid_cache_reuse/round1/job622335/timing_detail/chk*`：275 个意外长测检查点，从 chk00000064 到 chk00017600，合计 31,547,308,421 字节（约 29.38 GiB）。分析器跳过 timing_detail，这些检查点不作为已接受结果的输入；command.txt、run.log、冻结程序、输入、源码、回归和计时报告仍需保留。精确路径见上述目录的 cleanup_preview.json，不以通配符扩大删除范围。

本次临时脚本仍承担复现职责，全部保留。用户原有两份文档修改、六项 supervisor 文件删除及未跟踪 PPTX 均未纳入提交。没有执行任何数据删除。

[neat-freak 规则](../../../../../../.agents/skills/neat-freak/SKILL.md)要求“只有用户看完汇报后明确确认可以清场，才执行删除”。复核现场仍保留，等待用户确认后清场。

## 验证

本次为文档同步：检查修改文档的本地链接与锚点、diff 格式、原有用户 diff 保持一致，确认无源码或运行配置改动。运行事实来自本轮调度查询及既有冻结日志/报告；未重复 GPU 性能实验，不把文档检查当作新数值验证。知识入口已同步，工作区未宣称 cleaned。

## 用户确认后的清场复核（2026-11-02）

用户在完整预览汇报后明确要求“删除清理候选”。执行前逐项核对 cleanup_preview.json：275 个目录均位于指定 timing_detail 下、无软链、无 Git 跟踪文件，文件字节总数与预览一致。仅删除清单中的目录，共 31,547,308,421 字节；复核剩余 chk* 目录为 0。command.txt 与 run.log 的 SHA256 删除前后相同，其他冻结材料及用户原有修改保留。结果凭证在同一审计目录的 cleanup_result.json。上述“未删除/等待确认”是首次汇报时的历史状态，现由本节更新；二维风险等验证边界仍未消除。
