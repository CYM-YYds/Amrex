# ParaView 知识收尾

范围：2026-10-27 的 AMR 涡量修复、现有切片原位修复、断线诊断和日志留存。
现役操作与机制唯一入口为 [ParaView skill](../../.agents/skills/paraview/SKILL.md)，
具体操作见其 [GUI 桥接说明](../../.agents/skills/paraview/references/bridge.md)；本记录只保存收尾证据。
不扩大到求解器结构审查、其他算例或整个 workspace 内容审计。

## 事实面与证据

| 事实面 | 状态 | 核对结果与边界 |
|---|---|---|
| 代码 | verified-current | 桥版本 7；两种切片入口复用同一掩膜修复；服务自动保留日志；本次不改执行代码 |
| 运行态 | verified-current | 实时 client_status=ready，版本/源码哈希匹配；四个 pvserver 在编译节点运行，无遗留 pvbatch；Slice3(14535) 输入为修复过滤器(20674)，读取器为 lev2 |
| 文档 | changed-and-verified | 区分新建视图与原位修复、状态保存、日志覆盖边界、断线 pending；同步历史脚本索引的版本与解释器说明 |
| 规则 | verified-current | 根 AGENTS.md→CLAUDE.md、.claude/skills→.agents/skills、两个后处理兼容入口软链有效；未改规则或新建平行约束 |
| 记忆 | out-of-scope | generated-read-only；早期“6.2.0 单进程”是历史快照，现行安装 MPI/Python 均开启；未获明确记忆写入请求，不修改生成索引或 correction input |
| 工作区 | verified-current | 机械盘点 Markdown、规则链和唯一 worktree；本次 ParaView 实现均已提交；两处用户求解器修改保留；科研材料与清场候选均未删除 |

文档同步：桥接说明不再要求必须新建视图，说明原位修复后需保存新的 `.pvsm`；明确
Windows Output Messages 与 SSH 客户端错误不在服务日志内、日志不自动生成调用栈，退出码
0 不代表没有协议错误。集群公共 5.11.1 为 MPI 开启/Python 关闭，与私有 6.2.0 并存。
两份历史后处理 Markdown 改为 LF，并清除两处无语义尾空格；其余历史工具计算逻辑不变。

本次只改文档，验证本地链接/入口路径、软链、普通 diff 检查及 CRLF 归一化后的语义差异；
不重复已通过的单/四进程数值回归，不把文档检查当作新增数值验证。

## 保留材料与清场预览

本任务的输入路径、数值 JSON、命令、复现脚本和成功/失败日志均保留在
`projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/paraview_amr_gradient/`。
本次盘点、实时状态和服务进程快照保存在其 `knowledge_closeout/` 子目录。
`.tools/paraview-mpi/logs/` 的三份服务日志保留；没有停服或移除旧 Gradient 对照分支。

唯一明确的清场候选为 `/tmp/paraview-amr-gradient-check/`（约 192 KiB，上游源码解包副本，
可从保留的 ParaView 源码归档重建），以及已复制到任务材料目录的 `/tmp/neat-paraview-*`
和 `/tmp/paraview-slice3-inspect.json` 快照。未发现项目内本次任务仍需删除的独立临时脚本。
按 neat-freak 的汇报后确认要求，本次仅预览，候选与复核现场全部保留。

## 未闭合事项

- pending：Windows GUI 的 Tag mismatch 根因、修复图像的用户验收，以及用户是否已保存新版场景。
- warning：当前服务日志仍有色标 `%.2g` 弃用提示；未将其当作断线根因，也不为消除提示改图。
- out-of-scope：两处用户求解器修改、全仓数值正确性、各层块边界梯度精度、生成记忆写入、
  推送/PR、集群公共软件更改及破坏性清场；本次未执行这些动作。
