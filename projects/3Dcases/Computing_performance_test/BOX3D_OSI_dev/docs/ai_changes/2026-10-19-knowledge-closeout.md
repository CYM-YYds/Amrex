# BOX3D_OSI_dev 知识收尾（2026-10-19）

范围：近期二三维兼容、访问器及静态粒子整理；只同步文档和输入注释，不修改数值路径。
现役使用说明统一在 [README](../../README.md)，历史验收保留在各日期变更记录。

| 事实面 | 状态 | 核验与处理 |
| --- | --- | --- |
| 代码 | verified-current | HEAD c924a62；访问器区分 host/device，二维仅静态圆盘，流固耦合仍为三维 |
| 运行证据 | verified-current | 读取最新生成代码及数值 acceptance.json；现有 2D/3D 程序 SHA256 与 615156 冻结副本一致；未重跑计算 |
| 文档 | changed-and-verified | README 补最新复核入口、历史与当前范围、FAILED 汇总原因；两份输入只改注释，实际参数不变 |
| 规则 | verified-current | 根 AGENTS.md -> CLAUDE.md 有效，无算例覆盖规则；不另建重复规则 |
| 记忆 | out-of-scope | generated-read-only；只读检索旧案例索引，未写入或把旧案例证据当作当前验收 |
| 工作区 | pending | 单一 worktree，已有提交位于 detached HEAD；本次提交保留该状态，分支归属待用户决定 |

## 证据与未完成项

- 生成代码 18 项通过；42 个主场景与 12 个修改前控制复核通过；最终粒子回归 69 项通过。详见 [本日开发记录](2026-10-19-dimension-accessors.md)。这些结果均限于记录中的输入、布局和阶段。
- physical ghost 源差异仍保留，最大 0.02019539653；旧程序控制相同。进一步归因 pending，本轮不修数值。
- max_level > 1 的源码历史警告未移除；README 区分其与限定范围的通过证据。
- 新脚本把原两作业合为单作业，已做语法检查，尚未整套重新提交；不能声称新的合并作业已完成。
- 长期物理收敛、性能、跨节点和 GPU-aware MPI 验收 out-of-scope；无部署服务，发布/live 核验 not-applicable。
- 两份旧日期变更记录是当时快照；其中“当前程序”及粒子功能限制均按记录日期理解，现役接口以 README 为准。

## 清场预览

机械盘点算例非 runs/logs/tmp_build_dir 文件 136 项，Markdown 4 项（新增本记录前）。
候选仅 tests/__pycache__/ 内四个可再生成 .pyc，共 35746 字节；未删除。
保留 runs/、logs/、checkpoint、修改前基线、两轮 codegen 报告、原失败检查器与报告、构建产物及当前编译数据库，供复核及后续构建。
未改动相邻 BOX3D_OSI、全局规则、shell 配置或记忆；未推送或合并。
复核现场仍保留，等待用户在汇报后明确确认才清场。
