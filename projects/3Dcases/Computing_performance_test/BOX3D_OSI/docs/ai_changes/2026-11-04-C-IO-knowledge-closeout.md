# C/B 后处理与 A-B I/O 诊断知识收尾

范围：本会话BOX3D_OSI的阈值实验、图表与I/O诊断；不是整个workspace、代码结构或全仓库脚本审查。2026-11-04现场复核，无重提作业、数值代码或生产配置修改。

## 六个事实面

| 面 | 状态 | 依据与处置 |
|---|---|---|
| 代码 | verified-current | HEAD ErrorEst三维调用state_error_3，config/inputs无本轮未提交差异；纠正主README的state_error_6与“配置尚未提交”旧说法。冻结程序与当前源码分开解释 |
| 运行态 | verified-current | 调度器、退出回执、Header和日志确认C624717、B623697完成但未正式收敛；C阈值A-B长跑均失败，624899有EIO；625322三路径短测均成功，仅12个场文件一致证据 |
| 文档 | changed-and-verified | 以runs/README为运行终态入口，docs/current_status摘要转向它；后处理README补最新红色B对比图，脚本索引区分旧52个审计快照与当前56个文件；三路径方法记录更新终态 |
| 规则 | verified-current | 根AGENTS.md有效软链到CLAUDE.md；本算例无下级override；用户级AGENTS为空。未改规则或skills，未新增任务型规则 |
| 记忆 | generated-read-only | 未获直接更新记忆请求，不编辑MEMORY.md/生成摘要。旧记忆仅查找线索，旧512³未完成快照等以现场文档为准 |
| 工作区 | verified-current / pending | 起始盘点319份Markdown、1个worktree；本轮临时脚本均为当前诊断依赖或唯一复现入口，保留。用户已有修改及清场现场见下方；未删除 |

## 当前唯一行动依据

- 运行与故障终态：[计算结果入口](../../runs/README.md)。C OSI有40帧与性能日志，但无全程进程显存采样；不存在完整C阈值A-B/OSI性能和峰值显存对比，不能从失败运行拼成结论。
- 624899在step6400/t50对Cell_D执行writev，阻塞1200.851309秒后返回EIO。原始跟踪保留；这不单独确定存储客户端、网络、服务端或缓冲区因素。
- 625322在agent50同GPU顺序做三路径，各到6400步；12个对应场文件SHA256一致，原缓冲区直写共享盘也成功，故未复现故障，不把普通内存中转当成已证明修复。[诊断报告](../../runs/temporary/C_AB_IO_path_pair/job625322/report.md)。
- 图表与生成命令：[后处理入口](../../data_post_processing/README.md)。C/GIF、C/Cortes、C/512³/Cortes与最新红色B版本均保留，不用参考吻合替代收敛；uniform512来源623012而非失败622116。

## 保留与清场预览

没有删除、改名或迁移任何脚本、图、日志、输入、checkpoint、冻结程序、实验目录、分支或worktree。
checkpoint_io诊断脚本、heap_write.c及编译库为复现证据；a5入口仍是调用依赖，GIF合成器是唯一生成入口，均保留，不按temporary目录名判断冗余。

可选清场候选是节点agent50上的`/var/tmp/C_AB_IO_625322_kfdaajv9/`：共享盘已发布105个文件、共772596833字节并有传输清单。它仍是现场副本，应在节点内再次核对目录归属、是否仍被引用及共享清单后，再按用户确认处理；本轮未删除。共享报告、原始跟踪和失败文件继续保留。

用户已有工作区修改：删除的旧局部中心线图片、MLUPS记录、CCCM2026摘要草稿，以及演示稿pptx/生成脚本目录；不回退、不混入此次提交。未审查这些文稿/演示内容或其他算例。

## pending / out-of-scope / warning

- pending：长程EIO根因、长期稳定写出、C阈值A-B完整长跑及全程显存对比；本地现场清理需复核后确认。
- out-of-scope：用户文稿/性能表改动、PPT、其他算例、结构消重、生产部署；无本轮部署对象。
- warning：原失败PlotFile不完整，不后处理；短测成功不是修复证明；用户已删除旧局部图的索引引用暂保留为用户工作区事项；strace/普通内存复制不作纯性能数据。
- memory：只读，未写correction input；用户可另行明确要求更新记忆。

只读完整盘点、工作区快照与链接检查记录位于[runs/temporary/C_IO_knowledge_closeout](../../runs/temporary/C_IO_knowledge_closeout/README.md)。本轮只同步文档，不重跑数值实验；验证范围为事实对应、303个本地链接/锚点全部通过、规则同源与git diff --check。复核现场仍保留，清场在汇报后确认。
