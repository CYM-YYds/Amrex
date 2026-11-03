# 脚本维护核对与保留决定

核对日期：2026-11-03。范围为BOX3D_OSI scripts/temporary/的52个Python/Shell文件；现役脚本入口及调用关系也已检查，未做求解器结构审查。

## 与上轮索引的区别

上轮f03f10f仅建立导航，并未逐文件认定删除资格。此次枚举52个脚本，并检查scripts/tests/data_post_processing中静态路径与名字引用，筛查runs中的647个脚本快照，其中41个源脚本至少有一个SHA256完全相同的保留副本，16个有当前代码引用候选。扫描排除流场/checkpoint、src/config和构建对象等目录；动态生成命令、外部脚本和历史环境不是静态扫描能穷尽的，故无引用不等于无用。

例如analyze_uniform_dispatch.py仍被analyze_uniform_kernels.py导入，也被uniform_dispatch.sh复制，不能因已冻结副本而删。regrid_cache_reuse.sh仍复制/调用analyze_regrid_cache_reuse.py。a5_threshold_probe.sh、显存汇总与GIF合成仍是当前直接工作流。

## 逐文件清单

KEEP为现役依赖/已说明的复现材料；REVIEW为保留现场、待进一步裁决，均未删除。下表引用仅为候选；同名submit.sh等短名字不单独作为确定调用依据。完整路径证据见[静态检查记录](../runs/temporary/knowledge_closeout/20261103/script_reference_snapshot_review.json)。

| 文件 | 代码引用候选数 | 同SHA快照数 | 决定及理由 |
|---|---:|---:|---|
| [scripts/temporary/a5_threshold_probe.sh](../scripts/temporary/a5_threshold_probe.sh) | 0 | 5 | KEEP：当前实验/显存/GIF/Roofline工作流 |
| [scripts/temporary/analyze_a5_threshold_probe.py](../scripts/temporary/analyze_a5_threshold_probe.py) | 0 | 3 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/analyze_ghost_collision.py](../scripts/temporary/analyze_ghost_collision.py) | 0 | 2 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/analyze_regrid_cache_reuse.py](../scripts/temporary/analyze_regrid_cache_reuse.py) | 1 | 1 | KEEP：存在调用、复制或导入候选；复现依赖保留 |
| [scripts/temporary/analyze_regrid_quantification.py](../scripts/temporary/analyze_regrid_quantification.py) | 1 | 1 | KEEP：存在调用、复制或导入候选；复现依赖保留 |
| [scripts/temporary/analyze_regrid_stage_evaluation.py](../scripts/temporary/analyze_regrid_stage_evaluation.py) | 0 | 2 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/analyze_restriction_gpu_tags.py](../scripts/temporary/analyze_restriction_gpu_tags.py) | 1 | 0 | KEEP：存在调用、复制或导入候选；复现依赖保留 |
| [scripts/temporary/analyze_roofline_smoke.py](../scripts/temporary/analyze_roofline_smoke.py) | 0 | 0 | KEEP：当前实验/显存/GIF/Roofline工作流 |
| [scripts/temporary/analyze_transfer_fusion.py](../scripts/temporary/analyze_transfer_fusion.py) | 3 | 4 | KEEP：存在调用、复制或导入候选；复现依赖保留 |
| [scripts/temporary/analyze_uniform_dispatch.py](../scripts/temporary/analyze_uniform_dispatch.py) | 1 | 2 | KEEP：存在调用、复制或导入候选；复现依赖保留 |
| [scripts/temporary/analyze_uniform_kernels.py](../scripts/temporary/analyze_uniform_kernels.py) | 1 | 3 | KEEP：存在调用、复制或导入候选；复现依赖保留 |
| [scripts/temporary/build_transfer_restriction_gpu.sh](../scripts/temporary/build_transfer_restriction_gpu.sh) | 0 | 2 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/build_uniform_kernels.sh](../scripts/temporary/build_uniform_kernels.sh) | 0 | 3 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/checkpoint_io/submit.sh](../scripts/temporary/checkpoint_io/submit.sh) | 0 | 1 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/collision_amr/analyze.py](../scripts/temporary/collision_amr/analyze.py) | 1 | 0 | KEEP：存在调用、复制或导入候选；复现依赖保留 |
| [scripts/temporary/collision_amr/multigpu.py](../scripts/temporary/collision_amr/multigpu.py) | 1 | 1 | KEEP：存在调用、复制或导入候选；复现依赖保留 |
| [scripts/temporary/collision_amr/prepare.py](../scripts/temporary/collision_amr/prepare.py) | 0 | 0 | KEEP：原脚本有生成/分析实验的复现用途，未找到相同快照 |
| [scripts/temporary/collision_amr/q19_uniform.py](../scripts/temporary/collision_amr/q19_uniform.py) | 1 | 1 | KEEP：存在调用、复制或导入候选；复现依赖保留 |
| [scripts/temporary/collision_amr/stages.py](../scripts/temporary/collision_amr/stages.py) | 1 | 1 | KEEP：存在调用、复制或导入候选；复现依赖保留 |
| [scripts/temporary/collision_amr/submit.sh](../scripts/temporary/collision_amr/submit.sh) | 2 | 0 | KEEP：存在调用、复制或导入候选；复现依赖保留 |
| [scripts/temporary/collision_factors/analyze.py](../scripts/temporary/collision_factors/analyze.py) | 1 | 0 | KEEP：存在调用、复制或导入候选；复现依赖保留 |
| [scripts/temporary/collision_factors/prepare.py](../scripts/temporary/collision_factors/prepare.py) | 0 | 0 | KEEP：原脚本有生成/分析实验的复现用途，未找到相同快照 |
| [scripts/temporary/collision_factors/submit.sh](../scripts/temporary/collision_factors/submit.sh) | 0 | 1 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/flat_q27/analyze.py](../scripts/temporary/flat_q27/analyze.py) | 0 | 0 | KEEP：原脚本有生成/分析实验的复现用途，未找到相同快照 |
| [scripts/temporary/flat_q27/prepare.py](../scripts/temporary/flat_q27/prepare.py) | 0 | 0 | KEEP：原脚本有生成/分析实验的复现用途，未找到相同快照 |
| [scripts/temporary/flat_q27/submit.sh](../scripts/temporary/flat_q27/submit.sh) | 0 | 1 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/main_lifecycle_refactor_test.sh](../scripts/temporary/main_lifecycle_refactor_test.sh) | 0 | 2 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/pack_animation_gif.py](../scripts/temporary/pack_animation_gif.py) | 0 | 0 | KEEP：当前实验/显存/GIF/Roofline工作流 |
| [scripts/temporary/precision_switch/summarize.py](../scripts/temporary/precision_switch/summarize.py) | 0 | 2 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/precision_switch/validate.sh](../scripts/temporary/precision_switch/validate.sh) | 0 | 2 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/prepare_uniform_kernels.py](../scripts/temporary/prepare_uniform_kernels.py) | 0 | 3 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/profile_collision.sh](../scripts/temporary/profile_collision.sh) | 0 | 2 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/profile_uniform_hw.sh](../scripts/temporary/profile_uniform_hw.sh) | 0 | 0 | KEEP：原脚本有生成/分析实验的复现用途，未找到相同快照 |
| [scripts/temporary/regrid_cache_reuse.sh](../scripts/temporary/regrid_cache_reuse.sh) | 0 | 2 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/regrid_quantification.sh](../scripts/temporary/regrid_quantification.sh) | 0 | 2 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/regrid_stage_evaluation.sh](../scripts/temporary/regrid_stage_evaluation.sh) | 0 | 2 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/regrid_verification_boundary.sh](../scripts/temporary/regrid_verification_boundary.sh) | 0 | 1 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/restriction_tag_layout/analyze.py](../scripts/temporary/restriction_tag_layout/analyze.py) | 1 | 2 | KEEP：存在调用、复制或导入候选；复现依赖保留 |
| [scripts/temporary/restriction_tag_layout/prepare.py](../scripts/temporary/restriction_tag_layout/prepare.py) | 0 | 2 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/restriction_tag_layout/sizes.py](../scripts/temporary/restriction_tag_layout/sizes.py) | 0 | 2 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/restriction_tag_layout/submit.sh](../scripts/temporary/restriction_tag_layout/submit.sh) | 0 | 2 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/roofline_smoke.sh](../scripts/temporary/roofline_smoke.sh) | 0 | 1 | KEEP：当前实验/显存/GIF/Roofline工作流 |
| [scripts/temporary/single_precision_particles/check.sh](../scripts/temporary/single_precision_particles/check.sh) | 0 | 2 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/submit_restart_memory.sh](../scripts/temporary/submit_restart_memory.sh) | 0 | 6 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/submit_restriction_gpu_tags.sh](../scripts/temporary/submit_restriction_gpu_tags.sh) | 0 | 1 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/submit_transfer_fusion.sh](../scripts/temporary/submit_transfer_fusion.sh) | 0 | 4 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/summarize_restart_memory.py](../scripts/temporary/summarize_restart_memory.py) | 1 | 5 | KEEP：当前实验/显存/GIF/Roofline工作流 |
| [scripts/temporary/test_ghost_collision.sh](../scripts/temporary/test_ghost_collision.sh) | 0 | 2 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/uniform512_local_staging.py](../scripts/temporary/uniform512_local_staging.py) | 1 | 1 | KEEP：存在调用、复制或导入候选；复现依赖保留 |
| [scripts/temporary/uniform512_recovery.py](../scripts/temporary/uniform512_recovery.py) | 1 | 1 | KEEP：存在调用、复制或导入候选；复现依赖保留 |
| [scripts/temporary/uniform_dispatch.sh](../scripts/temporary/uniform_dispatch.sh) | 0 | 3 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |
| [scripts/temporary/validate_a5_plotfiles.py](../scripts/temporary/validate_a5_plotfiles.py) | 0 | 3 | REVIEW：相同快照已保留，但其独立可运行性/原路径依赖未全部核定 |

本轮未提出可直接删除的脚本，也未把52个文件全部认定为永久必需。现阶段的REVIEW表示后续可按实验组进一步审核，不能把它写成“全部必须保留”或“全部可删”。历史目录和冻结脚本不迁移，防止破坏__file__/相对导入/报告链接。

## 清场预览

仅发现两处本范围内可再生成的Python缓存目录，可在用户复核本报告后确认删除；本轮未删除，.py源文件不在清单中：

- `projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/__pycache__`：1个文件、5258字节，未跟踪、可再生成。
- `projects/3Dcases/Computing_performance_test/BOX3D_OSI/data_post_processing/__pycache__`：7个文件、92122字节，未跟踪、可再生成。

复核现场仍保留。依据neat-freak的“只有用户看完汇报后明确确认可以清场，才执行删除”，未在此次收尾中执行清场。其他算例缓存、已跟踪数据/日志/程序、运行输出和关键复现脚本均不在清理候选中。
