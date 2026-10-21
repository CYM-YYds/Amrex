# 二维兼容合并的知识收尾

目的：按 neat-freak 复核合并 fd9ede0 的现役入口、后续版本边界和历史归档，修复遗漏的编辑器引用。
范围：BOX3D_OSI 文档、`.clangd` 与审计说明；dev 归档只读。初始 HEAD=751b2ef，工作区干净。

| 事实面 | 状态 | 核对结果 |
|---|---|---|
| 代码/配置 | verified-current | 格子唯一入口 LbmLattice.H，构建默认 DIM=3；三维 Re=3200/BF16，二维 Re=1000/BF32。后续诊断改动不混作合并时源码。 |
| 运行态 | verified-current | 615942 的 10 个场景、615945 的 42 个主场景/12 个控制的 acceptance.json 均 passed；当前二维程序 SHA256 等于 615945 冻结程序，当前三维程序等于 616234/616235 manifest。调度查询无 RUNNING/PENDING/WAITING 作业，未发现 watch_job/guard_run/显存采样进程。 |
| 文档/编辑器 | changed-and-verified | README 补验证基线接口、三维专项预设边界及当前状态指针；current_status 区分历史合并验收与新诊断。`.clangd` 的 D3Q19.H 匹配改为 LbmLattice.H，审计白名单注释同步。 |
| 规则 | verified-current | 根 AGENTS.md→CLAUDE.md 软链有效，作用域内无更近覆盖，Codex 全局 AGENTS.md 为空；未修改规则。 |
| 记忆 | verified-current / generated-read-only | 生成索引已区分主算例与 dev 历史归档，本次仅作为定位线索；未获记忆更新请求，不写生成文件或 correction note。 |
| 工作区 | pending（清场预览） | 单一 worktree、detached HEAD；dev 原源码与证据、临时脚本和全部结果均保留。未推送、未删除分支/worktree。 |

## 改动与证据边界

- 现役机制仍集中在 README/架构，历史合并报告不改写；dev README 已有明确归档指向，无需复制第二套现役说明。IDE 中打开 dev/src 文件只用于查历史，新开发打开主算例 src 的对应文件。
- 当前二维程序仍是合并版本，未覆盖后来添加的加密诊断；“新诊断在二维重新构建/验收”保持 pending。后续二维运行先按 README 构建。本轮不修改求解器或编译/提交脚本，不重放 CFD。
- 当前三维程序与最新运行 manifest 一致，仅证明程序身份；不据此扩大 615942/615945 的历史逐值结论，也不将详细诊断作业作为性能或收敛证明。

## 清场预览

删除候选：`scripts/temporary/submit_merge3d_validation.sh`。它与
`runs/dimension_merge3d_615942/submit_merge3d_validation.sh` 逐字节一致，正式工具无调用；
现有引用均是历史复现说明。若后续删除工作副本，须同步说明保留的副本与原执行位置，
保留基线、冻结程序、命令、日志和报告。本轮只列候选，复核现场仍保留。
其他续跑任务的两个临时入口继续遵循其既有收尾记录，不在本轮扩大处理。
dev 目录仍被历史证据索引引用，不作为可直接整目录删除的候选。

pending：候选脚本清场；当前二维源码含新诊断的构建/验证。此前 I/O 根因和缺失显存样本仍以 current_status 为准。
out-of-scope：求解器结构/数值重审、长期收敛、性能、新 CFD、其他算例、全仓库审计、记忆写入及远端推送。
not-applicable：生产部署、页面/API 上线。
warning：detached HEAD；历史 physical ghost 差异仍为 0.02019539653，不等同于所有 ghost 全等。

验证：机械盘点主算例全部 Markdown（修改前 95 个）及 dev 归档文件；阅读受影响索引、规则、输入和入口，
检查当前程序 SHA256、两份验收报告及调度状态。`.clangd` YAML 解析与新旧路径正则匹配检查通过；
文档基线变量与现有脚本逐项核对，本地路径/锚点与 CRLF 感知 diff 检查通过。
未运行构建：编辑器配置和文档不改变构建配置或数值路径。
