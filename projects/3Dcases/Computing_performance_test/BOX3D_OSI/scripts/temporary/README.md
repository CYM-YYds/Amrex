# 任务专属脚本索引

返回[算例脚本入口](../README.md)。本目录共52个Python/Shell脚本（顶层32个）；另有候选CUDA源码和patch等实验材料。
下表为入口索引；逐文件静态引用、SHA256相同快照及KEEP/REVIEW决定见[维护审计](../../docs/script_maintenance_audit.md)。
下表用途说明不单独证明删除资格，目录名不授权删除。
新任务优先复用现有参数，不把下列任务驱动当作默认生产配置。

## 近期直接使用的工具

| 入口 | 用途及使用依据 | 保留状态 |
|---|---|---|
| [a5_threshold_probe.sh](a5_threshold_probe.sh) | A/B/C阈值、冻结程序、短测/长跑/重启、stream_modes对比；命令以各实验submit_command.txt为准 | 当前工作流 |
| [summarize_restart_memory.py](summarize_restart_memory.py) | GPU/逐PID显存采样汇总，a5_threshold_probe.sh调用并冻结副本 | 当前依赖 |
| [pack_animation_gif.py](pack_animation_gif.py) | PNG序列统一裁剪和GIF合成；C动画pack_command.txt记录具体参数 | 结果唯一合成入口 |
| [roofline_smoke.sh](roofline_smoke.sh)、[analyze_roofline_smoke.py](analyze_roofline_smoke.py) | Roofline小测试与统计，输出在runs/temporary/roofline_smoke/ | 结果复现 |

## 顶层完整清单

这里的“复现保留”表示脚本头或关联实验已表明复现用途，未重新验收历史环境；“待核”表示不能仅凭名称清理。

| 脚本 | 用途/线索 | 处理 |
|---|---|---|
| [a5_threshold_probe.sh](a5_threshold_probe.sh) | A5 对标前的阈值初测；本任务结束且不再需要复现时可删除，数据与报告保留。 | 当前工作流/复现保留 |
| [analyze_a5_threshold_probe.py](analyze_a5_threshold_probe.py) | 汇总本次 A5 阈值初测；保留用于报告复现，不再需要时可删除。 | 复现保留 |
| [analyze_ghost_collision.py](analyze_ghost_collision.py) | 本次实验的日志审计与计时汇总；保留为结果的复现入口。 | 复现保留 |
| [analyze_regrid_cache_reuse.py](analyze_regrid_cache_reuse.py) | 本次缓存/补边优化临时分析；作为报告复现材料保留，之后可删除。 | 复现保留 |
| [analyze_regrid_quantification.py](analyze_regrid_quantification.py) | 本次完整重构归因临时分析器；保留输入、日志与本脚本供复现。 | 复现保留 |
| [analyze_regrid_stage_evaluation.py](analyze_regrid_stage_evaluation.py) | 重构内部成本临时分析器；保留作为本任务复现入口，无依赖后可删除。 | 复现保留 |
| [analyze_restriction_gpu_tags.py](analyze_restriction_gpu_tags.py) | GPU restriction 建表实验分析；保留作报告复现入口，无后续依赖时可删除。 | 复现保留 |
| [analyze_roofline_smoke.py](analyze_roofline_smoke.py) | AI 临时分析：复算 Roofline 小测试的计时与逻辑带宽；保留复现依赖时保留。 | 复现保留 |
| [analyze_transfer_fusion.py](analyze_transfer_fusion.py) | 本次融合实验的临时分析器；保留为逐值回归和交替计时报告的复现材料。 | 复现保留 |
| [analyze_uniform_dispatch.py](analyze_uniform_dispatch.py) | 本次单层调度实验的临时汇总；随结果保留，用于复核逐值检查与配对计时。 | 复现保留 |
| [analyze_uniform_kernels.py](analyze_uniform_kernels.py) | 本次同步/无mask独立实验的临时汇总；复用上一轮的计时解析和rank聚合。 | 复现保留 |
| [build_transfer_restriction_gpu.sh](build_transfer_restriction_gpu.sh) | 本次融合的临时 GPU 对照构建；保留为实验复现材料，在编译节点执行。 | 复现保留 |
| [build_uniform_kernels.sh](build_uniform_kernels.sh) | 本次单层同步/无mask实验的临时构建入口；随结果保留供复现。 | 复现保留 |
| [main_lifecycle_refactor_test.sh](main_lifecycle_refactor_test.sh) | 本次 main 生命周期整理的短程回归；保留为测试结果的复现入口。 | 复现保留 |
| [pack_animation_gif.py](pack_animation_gif.py) | 本次动画合成临时工具；作为结果唯一生成入口保留，任务结束且无需复现时可删除。 | 当前工作流/复现保留 |
| [prepare_uniform_kernels.py](prepare_uniform_kernels.py) | 本次单层同步/无mask实验的临时源码生成器；随实验结果保留供复现。 | 复现保留 |
| [profile_collision.sh](profile_collision.sh) | 专项实验入口；按同名实验报告及脚本内参数核对 | 保留待核 |
| [profile_uniform_hw.sh](profile_uniform_hw.sh) | 专项实验入口；按同名实验报告及脚本内参数核对 | 保留待核 |
| [regrid_cache_reuse.sh](regrid_cache_reuse.sh) | 本次缓存复用及速度补边优化的临时验证入口；作为结果复现材料保留，之后可删除。 | 复现保留 |
| [regrid_quantification.sh](regrid_quantification.sh) | 本次阶段归因和旧检查回放的临时矩阵；保留为报告复现入口，之后可删除。 与缓存优化矩阵职责不同；复用现有 MPI 启动工具，不复制启动实现。 | 复现保留 |
| [regrid_stage_evaluation.sh](regrid_stage_evaluation.sh) | 完整平均及 Remake 内部分项评估临时入口；任务后无复现依赖时可删除。 与缓存优化矩阵职责不同；复用现有 MPI 启动工具，不复制启动实现。 | 复现保留 |
| [regrid_verification_boundary.sh](regrid_verification_boundary.sh) | 本次重构检查职责调整的临时回归入口；结果留存期间用于复现，之后可删除。 | 复现保留 |
| [roofline_smoke.sh](roofline_smoke.sh) | 专项实验入口；按同名实验报告及脚本内参数核对 | 保留待核 |
| [submit_restart_memory.sh](submit_restart_memory.sh) | AI 临时工具：本次双卡续跑的显存采样；任务结束后无后续依赖时可删除。 | 复现保留 |
| [submit_restriction_gpu_tags.sh](submit_restriction_gpu_tags.sh) | GPU 建表候选临时验证；保留本入口用于结果复现。 | 复现保留 |
| [submit_transfer_fusion.sh](submit_transfer_fusion.sh) | 专项实验入口；按同名实验报告及脚本内参数核对 | 保留待核 |
| [summarize_restart_memory.py](summarize_restart_memory.py) | AI 临时工具：汇总本次续跑显存采样；运行目录保留副本以复现峰值。 | 当前工作流/复现保留 |
| [test_ghost_collision.sh](test_ghost_collision.sh) | 专项实验入口；按同名实验报告及脚本内参数核对 | 保留待核 |
| [uniform512_local_staging.py](uniform512_local_staging.py) | 临时512³本地落盘/校验转存入口；独立管理分段恢复和临时盘生命周期，结果保留供复现。 | 复现保留 |
| [uniform512_recovery.py](uniform512_recovery.py) | 本轮临时512³恢复验证与正式重跑；保留本脚本用于复现，任务结束后按需清理。 | 复现保留 |
| [uniform_dispatch.sh](uniform_dispatch.sh) | 专项实验入口；按同名实验报告及脚本内参数核对 | 保留待核 |
| [validate_a5_plotfiles.py](validate_a5_plotfiles.py) | 复核本次 A/B PlotFile 完整性与可读性；保留用于复现，不再需要复现时可删除。 | 复现保留 |

## 子目录完整清单

这些是特定实验/验证的驱动或分析材料，按组保留；本轮只建立路径索引，未对每个历史分支重做数值审查。

| 子目录 | 脚本 | 处理 |
|---|---|---|
| [checkpoint_io](checkpoint_io/) | [submit.sh](checkpoint_io/submit.sh) | 保留；复现用途/后续依赖按实验记录核对 |
| [collision_amr](collision_amr/) | [analyze.py](collision_amr/analyze.py)、[multigpu.py](collision_amr/multigpu.py)、[prepare.py](collision_amr/prepare.py)、[q19_uniform.py](collision_amr/q19_uniform.py)、[stages.py](collision_amr/stages.py)、[submit.sh](collision_amr/submit.sh) | 保留；复现用途/后续依赖按实验记录核对 |
| [collision_factors](collision_factors/) | [analyze.py](collision_factors/analyze.py)、[prepare.py](collision_factors/prepare.py)、[submit.sh](collision_factors/submit.sh) | 保留；复现用途/后续依赖按实验记录核对 |
| [flat_q27](flat_q27/) | [analyze.py](flat_q27/analyze.py)、[prepare.py](flat_q27/prepare.py)、[submit.sh](flat_q27/submit.sh) | 保留；复现用途/后续依赖按实验记录核对 |
| [precision_switch](precision_switch/) | [summarize.py](precision_switch/summarize.py)、[validate.sh](precision_switch/validate.sh) | 保留；复现用途/后续依赖按实验记录核对 |
| [restriction_tag_layout](restriction_tag_layout/) | [analyze.py](restriction_tag_layout/analyze.py)、[prepare.py](restriction_tag_layout/prepare.py)、[sizes.py](restriction_tag_layout/sizes.py)、[submit.sh](restriction_tag_layout/submit.sh) | 保留；复现用途/后续依赖按实验记录核对 |
| [single_precision_particles](single_precision_particles/) | [check.sh](single_precision_particles/check.sh) | 保留；复现用途/后续依赖按实验记录核对 |

## 结果与脚本的位置

- 科学数据、日志、checkpoint、图表和冻结脚本副本保留在 ../../runs/ 对应实验目录。
- C GIF的原始渲染器随B历史结果保留；C的场景、批处理与合成命令见 [C动画记录](../../runs/a5_C_t1000_gif/job624717/C/figures/animation_C/README.md)。
- 顶层候选 `.cu`、`.patch` 及其他辅助文件不计入52个脚本，作为实验支持材料保留，未据此授权删除。
- 2026-11-03维护审计没有批准删除脚本；部分相同副本仍为REVIEW，继续保留现场。没有移动、合并或删除旧入口。
