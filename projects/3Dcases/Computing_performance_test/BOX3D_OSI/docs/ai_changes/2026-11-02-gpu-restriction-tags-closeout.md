# GPU restriction 建表知识收尾

目的：将 `11c7450` 的实现、冻结证据及当前可执行文件状态同步到现役入口，避免继续把 `5373085` 的程序和成本报告当成当前全量基线。范围仅 BOX3D_OSI 本次变更，不审计后续 ParaView 提交、其他算例、远端发布或 GUI 运行态。

## 事实面与处理

| 事实面 | 状态 | 核对与处理 |
| --- | --- | --- |
| 代码 | verified-current | `BuildRestrictionWork` 用局部区域描述和单 GPU kernel 展开标签，消费端及缓存所有权不变；无实验开关/计时字段；当前 src 与 integration/src 快照逐文件一致。 |
| 运行态 | verified-current | 当前三维 FLOAT 程序 SHA256 与 623351 相同；623333、623351 调度终态 SUCCEEDED，冻结报告和日志保留。属于本地程序及历史实测核验，没有部署或 live service。本轮不重跑数值/性能实验。 |
| 文档 | changed-and-verified | current_status 新增现役入口，旧 5373085 基线改为冻结历史；机制归入架构文档，README 只保留短说明及 Q9 映射修正/pending 边界。详细数据仍唯一指向原 GPU 建表记录。 |
| 规则 | verified-current | 根 AGENTS.md 有效软链指向 CLAUDE.md；实际层级未发现算例 override，规则引用存在；无须新增规则。未提交用户修改保持原样。 |
| 记忆 | pending | generated-read-only；只读核对 MEMORY 索引。旧 split-tag 实验与本次 GPU 生成标签不同，不能替换其历史结论；旧 Q9 32 线程风险笔记已落后于源码修正，二维验证仍 pending。未获明确写记忆请求，不改生成文件或新增 correction note；现役答案以本仓库文档为准。 |
| 工作区 | pending | 一个 worktree；已有推进文件差异未纳入本提交；本任务输入、源码、程序、日志、checkpoint、报告、复现入口均保留。候选清场尚未执行。 |

根只读盘点机械枚举 591 个 Markdown/MDX；内容检查限根 README/规则及本次命中的算例文档。检查时仓库 HEAD 为 `eec50bf`，后续三个 ParaView 提交不修改本任务求解器源。当前程序及源快照包含用户未提交的 `AmrCoreLBM_advance.cpp` 修改，不是纯提交源码的干净构建。其差异 SHA256 为 `dffd3038122ab65e19e835df178a2f91a4537666246de8b72ce379f4693dc039`，本轮保持不变。

## 保留与清理预览

下列路径均相对算例根，除显式 /tmp 路径。原始实验和唯一复现材料不作删除候选：

- `runs/temporary/restriction_gpu_tags/round1/job623302/`、`runs/temporary/restriction_gpu_tags/round2/job623333/`、`runs/temporary/restriction_gpu_tags/integration/job623351/` 与各自冻结源码、输入、程序、原始日志和报告全部保留。
- `runs/temporary/restriction_gpu_tags/round2/job623331/` 及首次提交/编译失败日志保留，用于解释测试门禁修正和构建路径；不能因失败而判废。
- 两个 `scripts/temporary/*restriction_gpu_tags*` 是本任务报告和运行复现入口，继续保留；不是无依赖的一次性脚本。
- 可清理候选：`runs/temporary/restriction_gpu_tags/round1/tmp_build_dir/`，约 136 MiB，可从保留源和构建配置重建的对象缓存；删除会使后续复编重新生成对象，不删除其源码、配置或程序。
- 可清理候选：`/tmp/restriction-user-advance-before.patch`，约 13 KiB，已与 `runs/temporary/restriction_gpu_tags/integration/user-advance-before.patch` 逐字节核对一致，后者保留；本轮未发现项目文档/脚本依赖该 /tmp 路径。

复核现场仍保留，等待用户确认后清场。依据本次调用的 neat-freak SKILL.md：“只有用户看完汇报后明确确认可以清场，才执行删除并补充汇报清场结果。”本轮没有删除分支、worktree、缓存、数据或补丁。

## 验证与边界

检查新增/变更文档链接和路径、规则软链与引用、`git diff --check`；核对现有原始报告的数值范围、程序指纹及源快照，无需为纯文档变更重新编译或跑性能矩阵。源码、构建配置和输入未改变。

pending：二维 Q9、DOUBLE、多节点、任意 AMR 层级及长期收敛的新增验收；生成记忆尚未整合本次现役变化；候选清场未执行。

out-of-scope：其他算例、ParaView 启动/连接、全 workspace 内容审计、远端 push/PR/部署。

未消除 warning：双 GPU 计时样本范围重叠，0.41% 均值改善不能升级为稳定多 GPU 加速；标准构建入口的 SSH 关闭问题没有在本轮修复。性能和验证事实详见[原记录](2026-11-02-gpu-restriction-tags.md)。
