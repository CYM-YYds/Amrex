# 计时与检测精简知识收尾

## 目的与范围

以 `43aa4ca` 为当前代码基线，核对 `e9258b8` 计时/检测整理和后续外层入口统一的文档与验证边界。
本轮只修改 BOX3D_OSI 的 README、架构、当前状态和本记录，不改变求解器、配置、脚本或记忆。

## 对齐结果

- README 保留日志单位和退役字段的唯一使用约定，并链接检测机制及冻结验证记录；
  性能绘图入口说明改为兼容新旧日志，保留缺失旧细分字段绘为 0 的解释。
- 架构说明补齐固定验证参数唯一来源、checkpoint 局部分量分类的生命周期与同步边界，
  明确 rank 本地分类与全局范数的范围差异；补明 `CheckConvergence` 不计入窗口 total。
- 当前状态增加精简变更检索入口，明确 621819/621975 的结论只覆盖冻结版本，
  不能把此前 +0.002711% 配对中位差当作最新入口统一后的性能验收。

| 事实面 | 状态 | 核对结论 |
| --- | --- | --- |
| 代码 | verified-current | 当前 19 份源码 SHA256 与入口统一实验的 source_sha256.json 全部一致 |
| 运行态 | verified-current | 正式 FLOAT 程序 SHA256 与 production_install.json 一致；621977 周期/非周期日志均正常结束 |
| 文档 | changed-and-verified | 本轮改动文档的相对链接、路径、当前符号和验证边界核对通过 |
| 规则 | verified-current | 根 AGENTS.md → CLAUDE.md 软链有效；目标路径无新增下级覆盖；规则无需修改 |
| 记忆 | out-of-scope | Codex 生成记忆只读；本轮没有直接更新记忆的授权 |
| 工作区 | pending | 开始时无未提交修改，仅一个 worktree；检查期间出现六个监督凭证删除，本轮不归因、不恢复、不纳入提交 |

## 验证与保留

- 使用 neat-freak 只读盘点入口机械枚举收尾前本算例 215 份 Markdown 和实际规则链；按本次变更命中文档核对，未做全仓内容审计。
- 当前正式程序指纹为 `4b670202083632f4869a447a6c7b4f8281dd1e504db1da7ee2ebe921e7ae3c3a`；
  历史计时候选指纹与入口统一实验记录的 previous_sha256 相同，未被当作当前程序。
- 本轮仅做文档/指纹/运行凭证检查，不重复提交 GPU 数值或性能作业。
- 保留 `runs/temporary/timing_verification_cleanup/round1/`（约 712 MiB）与
  `runs/temporary/uniform_dispatch_removal/round1/`（约 492 MiB）的输入、程序、源码、命令、日志、检查点与报告。
  它们仍有复现用途，不是待删除构建残留；已跟踪的验证凭证保持原位。
- 清场 pending：本会话 `/tmp/diagnostics-checkpoint-before.txt`、`/tmp/timing-cleanup-main-final.cpp`
  是重复对照副本，可在复核后删除；本轮只预览，复核现场仍保留，等待用户确认后清场。
- 外部工作区变更 pending：`logs/submit/621586-supervisor.{json,pid}`、
  `621642-supervisor.{json,pid}`、`621699-supervisor.{json,pid}` 六个已跟踪文件在检查期间变为删除状态，
  本轮未操作它们；保留现场，归属及是否提交由其操作者核对。
- 未消除 warning：已有 `LbmLattice.H` always_inline 属性编译警告；本轮没有修改或抑制它。
- out-of-scope：最新入口统一后的性能矩阵，以及二维、DOUBLE、更深 AMR、多 rank 或长期物理精度的新增验收。
  既有历史验证保持各自冻结范围；无远端发布或部署动作。
