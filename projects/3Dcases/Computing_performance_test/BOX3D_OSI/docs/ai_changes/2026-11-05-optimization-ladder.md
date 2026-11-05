# C128000 单 GPU 优化累加复测

目的：从 `624717/C/chk00128000` 统一恢复，逐项展示指定优化对平均 MLUPS 的影响，避免串联不同历史作业得到貌似可归因的性能图。

范围仅 BOX3D_OSI。正式求解器、inputs 和已有程序不改；在独立源码快照中撤回选定优化，保留数值修复与其他现役机制，不称某个历史提交的精确重建。

复现入口为 `scripts/temporary/optimization_ladder/{prepare.py,submit.sh,run.py,analyze.py}`；原始材料位于 `runs/temporary/optimization_ladder/round1/`。

## 重建依据与边界

- A-B/OSI 用现役 `lbm.stream_mode=0/1` 分别执行，统一 FLOAT、D3Q27、BGK。
- 批量插值参考 `7c50e97` 的正式采用记录；对同一缓存任务和同一数值函数切换逐任务/批量发射，包括 stage 缩放。保留当前 A-B/OSI 共用传输和 restriction，不把其他优化混入此项。
- 碰撞批量提交只做过原型，正式路径仍逐 Fab；[历史完整求解器实验](2026-10-21-interpolation-collision-batch-solver-test.md)曾出现退化，不能将插值收益标为碰撞批量收益。
- 边界批量/薄面参考 `a46d4d1`、`347db9c` 和[直接构面](2026-10-15-boundary-direct-faces.md)。历史[完整物理面修复](2026-10-15-boundary-full-faces.md)扩大过部分 ghost 写入范围，本次保留修复。撤回时在相同域内 grown-Fab 范围构造厚度 `nghost+1` 的候选面、去重并逐任务发射；边界公式仍只写物理面。这是相同写入语义的受控厚面基线，不能解释为原历史程序的逐指令复刻。
- 通信隐藏参考 `a9f6076`，用现役运行开关控制。单 rank 无远端发送时回退，行间波动不能归因于隐藏 MPI。
- 线程数参考 `9f0754c`、`ee1a1bb`。将 256→128 线程与坐标复用分别列出，数值函数、掩码和同步保持不变。

## 统一实验条件

单节点单 rank、单 A100，从 step128000/t1000 恢复到 step133000，5000粗步；C阈值 `0.25 0.5 1 2`、基础128³、最高level2、max_grid_size=128/256/512、blocking_factor=16、grid_eff=0.7、每32粗步动态重网格。

关闭性能运行中的场/checkpoint输出、收敛检查和额外有限值诊断，关闭OSI嵌套同步计时；保留正常推进计时与同步。每500步输出一次，十个窗口都纳入平均。

平均MLUPS采用有效更新量之和除以 `perf(s).total` 累计秒数，含重网格、排除恢复和输出；有效更新量由各日志窗口的 MLUPS_total×total 恢复，受打印精度影响。不是最后窗口，不直接平均不同成本窗口的吞吐率。单次5000步，窗口不是独立重复。

分子沿用 `ComputeWeightedLatticeUpdatesPerCoarseStep()`：各层 `2^lev × (本层valid cells − 下一层valid cells/8)` 之和，最细层不扣除；逐粗步累加，排除ghost和被覆盖粗单元。不是碰撞kernel的launch_cells。`total` 不含 `STEP` 打印、窗口报告、初始化和文件输出。

先做从真实checkpoint恢复的64步动态AMR回归，逐文件比较同阶段完整valid DDF checkpoint；优化组对比OSI未批量基线。A-B/OSI既有数值差异单列，短测不证明长期物理精度或收敛。

## 验证与结果

作业 `626333`，节点 `whshare-agent-203`，NVIDIA A100-PCIE-40GB。CUDA12.8/GCC11.3/HMPI1.2、AMReX26.06、SM80、C++20、FLOAT、RDC、生产优化参数构建通过。

五个工程优化组各9个checkpoint文件SHA256与OSI基线一致；七次数值应用全部退出0并输出三级有限FinalState范围。A-B对OSI的64步完整DDF比较valid/active范数全0。这些是本次真实checkpoint短程回归，不外推长程等价。

全部七组各5000步正常退出，共70个500步窗口；每组逐窗口/逐层碰撞calls和launch_cells一致，加权有效更新量在日志打印精度内一致。

| 累加项 | 平均MLUPS_total | 相对上一项 |
|---|---:|---:|
| A-B基线 | 1214.52 | — |
| OSI | 1376.64 | +13.35% |
| 插值批量提交 | 1792.76 | +30.23% |
| 边界批量薄面、直接构面 | 1953.80 | +8.98% |
| 通信隐藏开关 | 1954.47 | +0.03% |
| 碰撞256→128线程 | 2132.35 | +9.10% |
| 碰撞坐标复用 | 2409.03 | +12.98% |

本轮末项吞吐为基线的1.9835倍；单次测量，不称稳定加速比。单rank通信隐藏回退，0.03%不作为通信收益。

[图表、PDF、CSV与完整复现命令](../../data_post_processing/results/optimization_ladder_C128000/report.md)，[PNG](../../data_post_processing/results/optimization_ladder_C128000/optimization_ladder.png)。图已目视检查，中文、数值及标签正常。Shell/Python语法、文档路径、diff空白检查通过。

最初汇总错误地强制要求监护器的 `completion_seen=true`，导致调度器作业末尾报错；OSI应用在监护器最后轮询前退出，原始日志已含finalized且应用/启动器退出码均0。已改为核对退出码、原始finalized与完整窗口并从既有日志恢复汇总，无重跑、无改写原退出记录；保留 `626333-submit.log`、原始监护记录和 `postprocess_recovery.json`。

保留输入、命令、源快照、源码差异、程序指纹、构建日志、原始运行日志、退出状态、回归checkpoint、报告与图表。构建对象属于可清理中间产物，原始checkpoint只读链接，不在本次清理范围。

## 碰撞批量另列补测

用户确认主图按已采用优化累加、碰撞批量单列。扩展同一prepare/run/analyze入口的 `--collision-extra`，提交仍用原submit入口加 `LADDER_EXTRA=1`，不复制新的提交脚本。

从job626333冻结源码扩展临时碰撞任务缓存：只保存grow(valid,nghost)与周期扩域交集、Fab几何、本地/phase索引；与现有布局缓存一起构建/clear，阶段执行时才获取状态、covered/interface和当前phase。复用现有TagVector和原碰撞公式/筛选/阶段末同步，不改正式求解器，也不新增生产开关。任务建表计入实际regrid时间。仅支持本轮单rank、OSI、256线程、无坐标复用的实验配置。

两方案均启用插值批量，边界优化/通信隐藏关闭，其他条件沿用主图。作业626391，whshare-agent-14，同节点同程序同GPU两对换序，各次独立从C128000恢复推进5000步。不用与主图节点不同的绝对MLUPS作差。

新程序构建通过；64步真实checkpoint动态AMR回归中，新程序对照的9文件与主图冻结对照一致，碰撞批量实验的9文件与本作业对照一致。扩展后的共享分析入口回读主图原始日志，七行统计、工作量和相邻变化与原报告逐值一致。

六次应用（两次数值、四次性能）全部退出0，作业生成completed标记。两组各20个500步窗口，平均MLUPS分别为对照1793.10、插值＋碰撞批量1606.64，吞吐下降10.40%，累计total耗时增加11.61%。两对吞吐变化分别为−10.408%和−10.389%，逐窗口/逐层calls、launch_cells相同，有效更新量在打印精度内相同。仅本配置两对测量，不外推其他网格、线程数、精度或多GPU。

正式路径和已采用优化主图保持不变；碰撞批量作为未采用实验单独展示。[额外实验报告](../../data_post_processing/results/optimization_ladder_C128000/collision_batch_extra/report.md)、[额外实验图](../../data_post_processing/results/optimization_ladder_C128000/collision_batch_extra/collision_batch_extra.png)。完整原始材料位于 `runs/temporary/optimization_ladder/round2_collision/job626391/`；源码、程序、命令、输入、日志、checkpoint与生成入口保留，仅清理本轮复制的构建对象。Shell/Python语法、文档链接及源文件diff检查通过。
