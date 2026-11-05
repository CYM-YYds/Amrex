# 四项独立优化与全部开启：分项耗时

原始材料：/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/optimization_ladder/round3_independent/job626450。节点：whshare-agent-82。

统一OSI、FLOAT/D3Q27、单rank/单A100、C阈值，从624717/C step128000恢复到133000。动态AMR每32步重构，关闭输出/收敛及嵌套OSI细分计时；通信重叠关闭，碰撞批量不采用。

基线：平均与插值逐Box/任务发射、边界逐厚面任务发射、碰撞256线程、无碰撞坐标复用。四项分别独立相对同一基线开启，最后同时开启。仅坐标复用保持256线程；全开为128线程+坐标复用。

平均仅切换跨Box批量启动与逐Box启动，覆盖界面平均和regrid完整平均；half-warp归约、block映射、GPU任务表构建、写回与同步不变。沿用前轮数值修复；不代表某一历史提交的完整复刻。

每次total为十个500步窗口的推进墙钟和，含重网格、排除恢复/输出及窗口报告；若两轮已完成，图表为两次5000步耗时的均值。分项沿用perf(s)阶段墙钟；其余=total−碰撞−边界−插值−平均−通信，包含重网格未分列部分、OSI相位推进与未分列调度等。未进行分项百分比相加推导加速。

| 配置 | 每次5000步平均total/s | 相对基线耗时 | 耗时减少 | 平均MLUPS_total |
|---|---:|---:|---:|---:|
| OSI 基线 | 208.826 | 100.00% | +0.00% | 1264.92 |
| 仅平均＋插值批量 | 147.635 | 70.70% | +29.30% | 1789.20 |
| 仅边界批量薄面 | 195.851 | 93.79% | +6.21% | 1348.73 |
| 仅碰撞128线程 | 203.125 | 97.27% | +2.73% | 1300.43 |
| 仅碰撞坐标复用 | 205.604 | 98.46% | +1.54% | 1284.74 |
| 全部优化开启 | 109.726 | 52.54% | +47.46% | 2407.36 |

各组先做真实checkpoint恢复后的64步动态AMR回归，逐文件核对同阶段完整valid DDF checkpoint，见validation.json。性能每窗口/每层calls与launch_cells及有效更新量一致；有效更新量由日志MLUPS_total×total恢复，受打印精度影响。

不以小差异证明稳定收益，也不以64步回归证明长期物理等价或收敛。基线中保留未列出的现役优化。

[累计分项图](time_breakdown.png) · [PDF](time_breakdown.pdf) · [窗口曲线](window_times.png) · [逐窗口CSV](windows.csv)

复现（仓库根执行）：

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/optimization_ladder/analyze.py /home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/optimization_ladder/round3_independent/job626450 --independent --output-dir /home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/data_post_processing/results/optimization_independent_C128000
```

复测计划/原因：first thread-only run changed from ~19s to ~25s per500steps with unchanged work counts; cause unconfirmed。同分配内按反向顺序复测全部六组，记录GPU遥测。两轮全部保留，不剔除波动样本。表中total和分项为每次5000步的两轮均值，图中误差线/带为两次范围，不是置信区间。逐次结果见summary.json的individual_runs，逐窗口原始数值见windows.csv。

每轮执行顺序：首轮基线→平均插值→边界→线程→坐标→全开；第二轮反向。离线绘图只读已完成结果；仅计划指定的CCS作业在检测到recheck_plan.json且未完成时，接续执行run.py --independent --recheck，保证复测在原节点/GPU，见recheck_plan.json、gpu_recheck.csv。
