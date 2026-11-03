# 脚本导航：按任务找入口

日常从本页找入口，BOX3D_OSI 的脚本清单见[算例脚本索引](../projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/README.md)。
本页覆盖仓库公共入口及当前 BOX3D_OSI 工作流；其他算例查各自 README，AMReX 第三方源码不在此索引中。

## 我现在要做什么

| 任务 | 入口位置 | 在哪里运行 |
|---|---|---|
| 编译 BOX3D_OSI | [compile.sh](../projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/compile.sh) | 算例根目录；默认 FLOAT/3D |
| 提交 BOX3D_OSI | [submit.sh](../projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/submit.sh) | 算例根目录用 dsub 提交 |
| 带提交节点监护的提交 | [submit_guarded.sh](../projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/submit_guarded.sh) | 算例根目录直接运行，此脚本内部提交 |
| 提取中心线、画参考对比图 | [后处理 README](../projects/3Dcases/Computing_performance_test/BOX3D_OSI/data_post_processing/README.md) | 仓库根 .venv/bin/python；提取再选 batch/bridge |
| 画收敛曲线或性能图 | 同一[后处理目录](../projects/3Dcases/Computing_performance_test/BOX3D_OSI/data_post_processing/) | 仓库根 .venv/bin/python |
| C 阈值、A-B/OSI、峰值显存实验 | [临时实验索引](../projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/README.md) | 按冻结实验的 submit_command.txt 复现，不直接套用当前输入 |
| GIF 合成、C/B 动画来源 | [后处理 README](../projects/3Dcases/Computing_performance_test/BOX3D_OSI/data_post_processing/README.md) | ParaView 渲染后再用仓库 Python 合成 |
| Windows 连接 ParaView | [connect_paraview.cmd](../后处理脚本/connect_paraview.cmd)、[connect_paraview.ps1](../后处理脚本/connect_paraview.ps1) | Windows；两文件放一起 |
| ParaView 远端批处理/GUI 桥 | [ParaView skill](../.agents/skills/paraview/SKILL.md) | 远端执行在 whshare-agent-1；GUI 入口见 skill |
| 数值与工具回归 | [BOX3D_OSI/tests](../projects/3Dcases/Computing_performance_test/BOX3D_OSI/tests/) | 按算例 README 的具体测试命令 |
| 仓库级编译/提交底层实现 | [scripts/compile.sh](../scripts/compile.sh)、[scripts/submit.sh](../scripts/submit.sh) | 通常由算例入口调用 |
| 仓库同步、审计、清理 | [scripts](../scripts/) | 先读对应脚本；clean.sh 的实际删除目标须先检查 |

## 目录各自负责什么

| 目录 | 放什么 | 怎么辨认 |
|---|---|---|
| 根 scripts/ | 跨算例管理工具、公共构建/提交实现及 lib 依赖 | 不是 BOX3D_OSI 专属后处理目录 |
| 算例 scripts/ | 构建、提交、监护、实验驱动及内部 Shell 工具 | 每个入口与内部依赖见算例脚本索引 |
| 算例 data_post_processing/ | 中心线、收敛和性能分析的长期入口 | 顶层 5 个 Python 文件，其中 convergence_logs.py 是内部模块 |
| 算例 tests/ | 回归驱动、检查器、测试 fixture | 不是生产计算入口 |
| 算例 scripts/temporary/ | 任务专属实验、候选分析、临时构建与复现工具 | temporary 不等于可直接删除；完整清单见其 README |
| 算例 scripts/archive/、data_post_processing/archive/ | 历史特定实验入口 | 保留原复现语义，不作为默认新任务入口 |
| 算例 runs/、data_post_processing/results/ | 运行与图表结果、冻结程序/输入、复现命令和当时的脚本副本 | 查 README、manifest、command/submit_command、报告；副本不是当前正式入口 |
| 根 后处理脚本/ | Windows 连接入口、用户 ParaView 脚本与场景 | 专用脚本可能包含旧输入路径；用途见目录 README/文件说明 |
| .agents/skills/paraview/scripts/ | ParaView 桥接/批处理的维护实现 | 用户连接兼容入口与实现分开；按 skill 文档使用 |

## 当前 BOX3D_OSI 盘点范围

2026-11-03 按实体 Python/Shell/PowerShell/CMD 文件统计：scripts 顶层 10 个，递归 77 个；
其中 temporary 顶层 32 个、递归 52 个；tests 26 个；data_post_processing 顶层 5 个、递归 8 个。
本次仅补齐导航和用途标识，脚本数量与路径不变。未把 runs 中历史快照、其他算例或第三方源码纳入数量。

本轮保留现役入口及内部依赖；实验/历史工具保留复现用途或待核状态。没有认定出证据充分、
无引用、无后续用途的可删除临时脚本。未验证全部历史环境或逐个重新运行旧脚本。
目录与生命周期规则以[根 README](../README.md#脚本归属与生命周期)为准。
