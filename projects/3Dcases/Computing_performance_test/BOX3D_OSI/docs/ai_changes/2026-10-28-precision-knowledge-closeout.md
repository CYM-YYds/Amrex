# 精度切换与默认值知识收尾

日期：2026-10-28。审计起点：`1428410`，分支 `Amrex`，单一 worktree，任务开始时 Git 工作区为空。
范围：BOX3D_OSI 的精度入口、默认构建记录、受影响的 README/架构/交接状态及直接依赖的公共编译入口。
本次仅修改文档，不改代码、配置、规则、实验材料或生成记忆，不构建、不提交新作业。

## 对齐的事实

- `config/GNUmakefile`、编译与提交入口均默认 FLOAT；显式 DOUBLE 保留。公共脚本只转发显式精度，其他算例默认值不由本次变更决定。
- `main` 负责 AMReX 生命周期和实际精度打印，`RunSimulation` 及同文件辅助函数组织模拟；当前输出调用包括速度和涡量，独立密度输出调用仍被注释。
- README 的程序名和当前职责同步至源码，使用方法仍由 README 维护；架构文档负责传输与任务映射，current_status 负责冻结验收与当前差异。
- 将旧程序等同性和工作区描述限定为2026-10-27现场，补齐2026-10-28默认值和当前验收边界。
- Q27 仍调用半 warp 核。Q9 已在 `RestrictDdfToCoarse` 的非 Q27 分支调用标量核，但 `restriction_group_size=32`，父单元索引仍由 `threadIdx.x / 32` 得到；同组线程执行完整 Q 循环并写同一父单元，存在重复写入风险。将“当前 Q9 整 warp 归约已验收”改为真实分派和 pending，未擅自修复数值路径。

## 事实面与验证

| 事实面 | 状态 | 证据及边界 |
|---|---|---|
| 代码 | verified-current（静态） | 核对精度选择、公共参数转发、main 生命周期与输出、Q9/Q27 实际调用及任务分组；静态审计不等于数值正确性证明。 |
| 运行态 | pending（当前数值验收） | 默认三维 FLOAT 构建日志成功，数据库9项全部含 BL_USE_FLOAT；620466留存15个应用exit=0和汇总报告。当前程序指纹不同，未将旧数值/性能验收外推至当前HEAD，也未查询新的调度状态或启动应用。 |
| 文档 | changed-and-verified | README、架构及交接状态就地修正；校验本地链接与 diff。初版/默认值变更记录保留各自历史事实。 |
| 规则 | verified-current | 根 AGENTS.md 是指向 CLAUDE.md 的有效软链；根到算例无新增覆盖规则，引用的技能和规则路径有效，无需增加平行规则。 |
| 记忆 | out-of-scope（generated-read-only） | 生成索引仍记录初版默认DOUBLE，已按当前配置识别其历史范围；未获直接更新记忆请求，不修改生成文件或写 correction note。现役默认值以README/配置为准。 |
| 工作区 | verified-current（只读盘点） | 枚举158份Markdown及含忽略目录的19427个文件路径，按本次范围阅读受影响条目；单一worktree，无额外分支清场需求；审计期间出现的 `src/LagrangeParticleContainer.cpp` 用户修改保留、不纳入本次提交，未删除文件。 |

本次直接读取 `runs/temporary/default_float/build.log`、`config/compile_commands.json`、
`runs/temporary/precision_switch/job620466/status.tsv`、`summary.json` 和 `executable.sha256`。
再次计算的程序 SHA256：

| 对象 | SHA256 |
|---|---|
| 当前三维 FLOAT 程序 | `9838a91e911a9eebcfefbb7087beb51c04ccbcb392dd63e734a9162d9bd85caf` |
| 620466 冻结 FLOAT 程序 | `d67c66c10cd046b6138f7a5a978daddd3b19de2815ee1ef3d218105878395bb9` |
| 旧融合 candidate/app3d.ex | `0a46a96653266661e244d3f424ab55ef18ef087d8d16e79bb24eb0021a6fc133` |

不同指纹仅用于否定程序相同，不能据此认定程序有数值错误或推断源代码完全对应关系。
本次文档验证为143项本地链接、规则软链及引用、只读JSON/退出记录检查与文档范围的 `git diff --check`，不重跑历史实验。

## 保留与未决

- 保留 `runs/temporary/precision_switch/`、`runs/temporary/default_float/` 的输入、源码/程序快照、命令、日志、checkpoint和报告。
- 保留 `scripts/temporary/precision_switch/validate.sh`、`summarize.py`，仍是已提交验证记录引用的复现依赖；没有本次确认可删且无依赖的文件，不提出破坏性清场。
- pending：当前Q9 CUDA平均分派的重复写风险，需要数值修复与针对性验证；当前HEAD的长程精度、性能及其他旧记录中的未验收范围不由本次知识收尾消除。
- out-of-scope：其他算例、生成记忆更新、求解器结构审查/数值修复、历史目录归并、推送/PR/部署。
- 未消除的 warning：Q9分派与分组不一致；旧程序证据不可自动代表当前程序。无发布服务，部署状态 not-applicable。
