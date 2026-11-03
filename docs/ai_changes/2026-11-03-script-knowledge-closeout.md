# 脚本导航与近期实验知识收尾

范围：根脚本导航、BOX3D_OSI及其直接使用的后处理入口；非整个workspace审计。基线8ddedd4，开始时工作区无差异、单一worktree。随后外部对BOX3D_OSI/docs/MLUPS记录.md产生修改，并移除了images中旧A/B局部图汇总副本；这些外部变动保留且不提交本轮。

## 现役事实矩阵

| 事实面 | 状态 | 证据与边界 |
|---|---|---|
| 代码/入口 | verified-current | scripts顶层10个，temporary递归52个；当前ErrorEst调用state_error_3。仅检查本轮入口/参数，不做数值内核结构审查 |
| 运行态 | verified-current | C624717完成128000步/t1000；B623697完成256000步/t2000，均exit0。624739及624780/624781/624810/624811全部FAILED/exit6；最后两份重试也报VisMF写失败 |
| 文档 | changed-and-verified | 计算入口/交接状态增加最新事实，旧RUNNING回执明确为历史快照；脚本位置索引与删除资格审查分开；B动画迁移路径修复、缺失图片标历史 |
| 规则 | verified-current | 根AGENTS.md仍为CLAUDE.md有效软链，规则73行；无新增规则/技能、无迁移、无删除、未扩大其他算例范围 |
| 记忆 | out-of-scope | Codex生成记忆只读，未获用户直接写入请求，不修改MEMORY或生成摘要。旧记忆可能停留在旧作业状态，最新事实以运行日志与runs/README为准 |
| 工作区 | changed-and-verified | 本轮只改文档和审计证据；外部性能表修改及图片删除保留，不提交。清场预览待确认，未执行删除 |

没有部署/发布面，标not-applicable；没有提交新算例或重新渲染图像。

## 完成的核对与修正

1. 原neat-freak只读脚本枚举641篇Markdown、规则链、根目录及worktree。此为机械清单，不代表逐篇全文验收。原始清单保留于[审计证据](../../projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/knowledge_closeout/20261103/README.md)。
2. [52脚本逐项审计](../../projects/3Dcases/Computing_performance_test/BOX3D_OSI/docs/script_maintenance_audit.md)：647个冻结脚本候选中41个源文件有同SHA副本，16个有当前代码路径/名称引用候选；KEEP 25、REVIEW 27，均保留。REVIEW不等于永久保留或可直接删，静态扫描不穷尽动态/外部调用。
3. 更新[计算结果入口](../../projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/README.md)，区分C完整OSI结果、失败A-B对照、B延长计算、C/D短测、623012恢复成功的均匀512³。当前程序a098bb…与C实验冻结90ebe0…不可混用；无需新编译来完成本轮文档核对。
4. C GIF、仅C/Cortes、C/512³/Cortes为三个明确结果入口，原图保留。B GIF及40PNG已移动到gif-B，40个裁剪PNG逐个与原裁剪report.json的SHA256相符。
5. 图片索引中26个其他失效链接：21个改链仍存在的原图，5个标为文件未找到的历史记录；没有擅自补画或删除数据。详情见[图片链接核对](../../projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/knowledge_closeout/20261103/image_link_review.json)。

## pending / out-of-scope / 清场预览

- pending：重复VisMF写失败的底层原因；失败作业的局部显存峰值不视为t1000全程峰值。无新A-B/OSI完整性能结论。
- pending：27个REVIEW脚本的独立快照可运行性/动态引用未全部核定。未重放历史实验；旧驱动可能固定旧路径，例如B裁剪脚本仍按原输出目录glob，不直接作为现役重绘命令。
- pending：以下5个图片登记同时缺少汇总副本与登记原图，仅保留历史来源：
  - `job586660_data_post_processing__586660_convergence.png`
  - `job596154_data_post_processing__596154_Re3200_convergence.png`
  - `job615364_data_post_processing__615364_Re3200_convergence.png`
  - `job_unknown_data_post_processing__Re1000_3D_comparison_96000.png`
  - `job_unknown_data_post_processing__Re1000_3D_overlap_160000_fig18a.png`

- out-of-scope：其他算例、第三方源码、全workspace审计、数值回归/编译、服务部署、外部MLUPS文档与图片删除改动，以及生成记忆写入。
- 待确认清理：仅BOX3D_OSI/scripts/__pycache__/（1文件）和data_post_processing/__pycache__/（7文件），共97380字节，未跟踪、可再生成；源脚本、实验结果、输入、日志、图表与checkpoint均不在清单中。
- 复核现场仍保留，等待用户确认后清场。依据[neat-freak](../../.agents/skills/neat-freak/SKILL.md)：“只有用户看完汇报后明确确认可以清场，才执行删除并补充汇报清场结果”。本轮删除0、迁移0；脚本数量不变。

验证：实际作业状态/guard退出码/终态Header、目录与软链、52个索引覆盖、40个B动画PNG来源哈希、相关本地文档链接与锚点、git diff --check。共核对431个本地链接和40个Markdown锚点，全部通过。没有用git干净或已提交来代替数值正确性与运行完成判定。
