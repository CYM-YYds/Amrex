# BOX3D_OSI 构建、提交与实验脚本

返回[算例 README](../README.md) · [仓库脚本导航](../../../../../docs/script-guide.md) · [后处理入口](../data_post_processing/README.md)

## 日常入口

从算例根目录执行：

```bash
./scripts/compile.sh
CCS_CLI_HOME=/opt/batch/cli dsub -s scripts/submit.sh
# 或使用提交节点监护入口；它内部调用 dsub，不再外套一次 dsub。
./scripts/submit_guarded.sh
```

| 脚本 | 职责 | 分类 |
|---|---|---|
| [compile.sh](compile.sh) | 选择 DIM/PRECISION，调用仓库公共编译流程 | 用户入口 |
| [submit.sh](submit.sh) | 冻结输入、创建独立运行目录、处理重启并启动求解器 | dsub 提交入口 |
| [submit_guarded.sh](submit_guarded.sh) | 提交后启动提交节点监督器 | 用户入口 |
| [submit_re1000_128k.sh](submit_re1000_128k.sh) | 固定 Re1000/128000步的 ab 或 osi 实验，关闭 PlotFile/收敛监测 | 专用入口；不用于当前 C GIF 流场任务 |
| [submit_osi_lockstep.sh](submit_osi_lockstep.sh) | 64/128/256 粗步锁步逐阶段检查 | 验证入口 |
| [run_rank.sh](run_rank.sh) | 启动实际程序并记录各 rank 退出码 | submit.sh 的内部依赖 |
| [guard_run.py](guard_run.py) | 应用/启动器生命周期与超时监护 | 提交入口的内部依赖 |
| [watch_job.py](watch_job.py) | 提交节点查询完成凭证与调度状态 | submit_guarded.sh 的内部依赖 |
| [osi_experiment_common.sh](osi_experiment_common.sh) | 实验的 hostfile/MPI 共用函数 | 被 source 的内部依赖 |
| [osi_lockstep_args.sh](osi_lockstep_args.sh) | 锁步检查的共用输入参数 | 被 source 的内部依赖 |

## 专项实验与验证

- [temporary/README.md](temporary/README.md)：当前及历史任务专属实验脚本索引；包含 C 阈值、显存采样、Roofline、GIF 合成和二维/三维 IBM 回归。
- [experiments/cuda_aware/README.md](experiments/cuda_aware/README.md)：独立 CUDA-aware MPI 实验，不能直接视为当前生产默认环境。
- [archive/](archive/)：12 个历史提交脚本；按历史记录复现，默认输入/程序路径需单独核对。
- [tests/](../tests/)：26 个回归脚本；实际命令和所需基线见算例 README。
- [data_post_processing/README.md](../data_post_processing/README.md)：提取中心线、绘图、收敛/性能分析及动画。

实验结果位于 ../runs/，先查对应 README 与 submit_command.txt/command.txt；冻结脚本和程序用于复现，
当前入口用于新任务，两者的输入/版本语义以各自记录为准。现有路径保持不变。
