# README 文件架构与知识收尾

- 目的：让 README 提供当前源码导航和使用入口，避免历史验证叙事掩盖文件关系。
- 范围：仅 BOX3D_OSI 的 README、当前状态与架构文档；未改源码、配置、脚本、规则或记忆。
- 改动：补目录树、各编译文件职责与数据所有权、现役 Cycle2/AdvanceLevel 调用链、修改定位表；说明 D3Q19.H 实际为 Q=27、detail 定义已合入 AmrCoreLBM.H、Fab 上下文统一位于 OsiIndex.H。修正专项脚本快照概括，说明主提交脚本只链接 app。将历史性能/作业细节从 README 收敛为状态文档指针，架构职责表统一引用 README。
- 证据：核对 config/Make.package 全部源/头文件、inputs、主入口及推进实现、编译/提交脚本；核对 610666 四组完成日志及冻结 app SHA256，区分当前 app 与该冻结版本；160419 编译日志确认后续头文件合并的编译证据。当前环境无法直接查询调度状态（CCS_CLI_HOME 未设置），本次仅使用保存日志的完成标记，不冒称本次重新取得 SUCCEEDED 状态。
- 验证：三份文档 42 个本地链接及 Markdown 锚点全部通过；源码登记、路径、根 AGENTS.md → CLAUDE.md 同源检查通过；git diff --check 通过。纯文档变更未重复构建/运行。
- 事实面：代码 verified-current；文档 changed-and-verified；规则 verified-current；运行态 pending（冻结版本日志已核对，当前 app 无新矩阵验收）；记忆 out-of-scope/generated-read-only；工作区 verified-current（单 worktree，仅本次文档修改）；部署 not-applicable。
- 遗留：长程、多节点、重启、多层周期及当前 HMPI device-direct 维持 pending；四个历史提交入口引用缺失 inputs_osi，README 已标明，脚本修复 out-of-scope。非周期 physical-ghost source 差异保留原边界。运行目录、日志、checkpoint 和临时验证脚本均保留，本次无删除动作或删除候选；历史变更记录按当时版本保留，不写回生成记忆。
