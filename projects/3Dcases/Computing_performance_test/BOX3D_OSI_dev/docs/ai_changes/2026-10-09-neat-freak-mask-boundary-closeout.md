# 固定覆盖掩码与边界修复的知识收尾

- 目的与范围：仅同步 BOX3D_OSI 的 README、交接状态、架构说明和四份受影响历史变更记录；不改数值代码或其他算例。
- 现役入口：边界修复范围统一见 [架构文档](../osi_algorithm_and_architecture.md#101-完整平均后的边界修复)，配置与最新验证统一见 [交接状态](../current_status.md)。README 改为摘要和指针；历史记录明确标注已被固定掩码实现更新。regrid 图删除旧 q 分批描述，并区分通信缓冲与已移除的持久同步缓冲。
- 证据：重新查询作业 `610641` 为 `SUCCEEDED`，当前可执行文件 SHA256 与该作业冻结的 `app.ex` 相同；复核 24 组范围检查、64 步 direct/fallback 阶段结果和三层全部 valid DDF 基线比较。新增插值门禁后的有限窗口推进回归已完成，长程、重启、多节点、device-direct 和物理边界精度阶数仍未由这些检查证明。
- 规则：盘点当前算例全部 Markdown 和作用域规则；根 `AGENTS.md` 仍软链到 `CLAUDE.md`，本次代码/配置修改及证据记录均已分别提交。规则文件保持原样。
- 记忆：generated-read-only；旧索引关于 `cf_mask_mode` 和全部边界修复的说法只作为历史线索，当前事实以以上文档与源码为准。未获显式记忆更新授权，未写 correction input 或生成记忆。
- 状态：代码、运行证据、规则为 `verified-current`；文档为 `changed-and-verified`；记忆写入及其他算例为 `out-of-scope`；工作区清场为 `pending`，对外部署为 `not-applicable`。
- 验证：修改文档的本地链接和章节锚点、`git diff --check` 通过。纯文档修改无需重复数值构建。下列清场候选已按文件大小及冻结副本指纹只读核对，复核现场仍保留；须完整汇报后得到用户确认才能删除。

## 清场候选（相对于本算例）

| 候选 | 保留的依据或副本 |
|---|---|
| `runs/covered_boundary_repair_20261009/check_repair.ex` | 与同目录 `job610554/check_repair.ex` SHA256 相同 |
| `runs/fixed_covered_mask_20261009/check_repair.ex` | 与同目录 `job610641/check_repair.ex` SHA256 相同 |
| `runs/fixed_covered_mask_20261009/baseline.app.ex` | 与同目录 `job610641/baseline.app.ex` SHA256 相同 |
| 上述两个运行目录根部的 `check_repair.o`、`check_repair.d`，共四个文件 | 测试源码、测试头、编译脚本、日志和冻结的测试可执行文件均保留 |

七个候选合计约 358 MiB。历史运行目录、失败日志、输入、checkpoint、源码差异和数值验证汇总不列入删除候选。已有工作树中的 `2026-10-08-clangd-warp-intrinsics.md`、`coincident_level_interface_plan.md` 删除属于用户先前修改，不恢复、不纳入本次提交。

## 未闭合范围

生成记忆中的旧说法保持只读。既有四个专项脚本仍引用缺失的 `config/inputs_osi`，交接状态已提示不可直接作为验收入口；该历史脚本修复不属于本次掩码/边界知识收尾。长程、多节点、重启、device-direct 和物理精度验证保持 pending。
