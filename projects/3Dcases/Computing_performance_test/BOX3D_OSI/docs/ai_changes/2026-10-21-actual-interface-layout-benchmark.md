# 按实际 interface Box 集合复测平均内核

目的：纠正用假设的长条/立方盒代表实际平均区域的偏差，核实 `64×2×2` 是否出现，并在真实布局上重测 Q19/Q27 的线程映射。
范围：BOX3D_OSI；生产源码、输入、可执行文件不改。只在独立源码快照插桩采集布局，随后用独立程序重放 restriction。

## 判定

**当前采样中没有 `64×2×2`，所有实际平均盒至少有一个方向厚度为 1。单独改半 warp 在后期碎盒集合上没有收益；应优先验证跨盒批量启动。**
这里的“后期”只指本次 128 步采样末端，不表示物理稳态或长期布局分布。

该结果修正[前一次合成盒微基准](2026-10-21-restriction-q19-benchmark.md)对优先候选的判断：单盒的大尺寸收益不能代表真实碎盒调用。
实际数据同时包含大薄片、小薄片和小条带，不能只用一种长条尺寸概括。

## 实际布局采集

- 源码基线 `ac13e82`；当前生产 inputs，Re=3200、128³、max_level=2、max_grid_size=128、regrid_int=32、非周期腔流，1 rank/GPU，推进 128 coarse steps。
- 只关闭 PlotFile、checkpoint、收敛停止和 OSI 嵌套计时；不使用 `regrid_fixture`、不改变网格判据。
- 采集作业 616396 正常完成并输出 `AMReX finalized`。冻结版本只在 `AverageDownOsiLevel` 输出调用编号、层级、相位、scale/is_scale、粗盒坐标/尺寸、对应 fine Fab 索引及完整 grown ring；正式源码和主可执行文件从未替换。
- 用生产构建命令重新编译唯一插桩的 `AmrCoreLBM_osi.cpp`，复用当时的其他生产对象，重链接独立 `capture.ex`。构建命令、源码、差异、日志及可执行文件均保留。
- 作业 616393 的命令提取将编译包装器中重复的 `-c` 误当成源文件定位，构建失败；修正后才获得 616396。该失败不进入任何计时。

[完整几何统计](../../runs/interface_retest_20261021/geometry.json)、[原始调用/坐标记录](../../runs/interface_retest_20261021/capture_616396/boxes.txt)、[采集命令](../../runs/interface_retest_20261021/capture_616396/command.txt)、[采集源码差异](../../runs/interface_retest_20261021/capture.diff)。

227 次平均调用共包含 34,734 次 Box 启动，368 种精确形状；全部 34,734 次的最小边长为 1，`64×2×2` 及其方向置换为 0 次。
约 13.2% 的 Box 调用不超过 32 个父单元，约 63.0% 不超过 256 个。这里按实际调用频次计数，不是形状种类比例或耗时比例。

部分实际形状及调用次数：

| 形状 | 调用次数 |
|---|---:|
| 1×7×7 | 965 |
| 64×64×1 | 714 |
| 17×7×1 | 642 |
| 8×1×1 | 577 |
| 8×2×1 | 449 |
| 1×64×1 | 396 |

这与 `BuildAverageCache` 的 `grow(uncovered_box,1) & covered_box` 一层界面构造相符；其后 `removeOverlap` 去重/简化。没有把本次采样结果外推为所有可能配置、所有未来步数的形状清单。

## 重放方法与限制

取各层第一次/最后一次平均调用，保留整个 Box 集合、顺序、粗盒绝对坐标、fine ring 尺寸/stride、共享 fine Fab 的关系、相位和实际 scale：

| 调用 ID | coarse level | phase | Box 数 | 父单元总数 |
|---|---:|---:|---:|---:|
| 1 | 0 | 2 | 4 | 16,384 |
| 227 | 0 | 2 | 50 | 24,192 |
| 33 | 1 | 2 | 286 | 119,048 |
| 226 | 1 | 4 | 286 | 217,984 |

相位在重构后可能重置，不能把 phase 当作 coarse step。
Q19 使用已有 D3Q19 方向/权重 overlay；不修改正式 Q27 配置。两种 Q 都在实际尺寸的 fine ring 中编码解析的空间/方向非平衡正分布，**没有读取真实流场 DDF，也没有运行完整 Q19 求解器**。
CPU 参考用 `e_host` 独立计算八子单元缩放平均，GPU 只访问设备方向表；逐分量及每个父单元密度/动量残差阈值均为绝对 `1e-12`，拒绝 NaN/Inf 和未写输出。

逐盒方案包括原整 warp runtime、整 warp fixed、半 warp fixed、半 warp runtime、单线程/父单元以及 8空间×4分组。
此次保留 runtime 半 warp，避免把缩放常量分派的差异混入线程映射对照。

另测两种跨盒批量方案：生成每个原 block 的标签（源/目标视图、fine ring、粗盒坐标、盒内起始父单元），一次 GPU 启动提交全部标签。分别保留整 warp 或半 warp 组织。
批量方案没有跨盒重新打包父单元，原盒内 block 划分、尾部 padding 和数值公式保持不变；所有写入盒相互独立。
标签构建、分配、上传在计时外完成，真实集成时必须实现布局生命周期内复用，不能每步重建后忽略成本。
批量启动同时改变 CPU 提交次数、小 kernel 间的调度边界和不同盒 block 的并发机会；没有 profiler 分解，不能把全部收益归因于 CPU 启动耗时。

主结果作业 616399，`whshare-agent-56`，NVIDIA A100-PCIE-40GB。CUDA 12.8/GCC 11.3、SM80、FP64、RDC、`-O3 --use_fast_math`、256 threads/block、ratio=2。
9 轮正反交替，每轮连续重放完整 Box 集合 10 次；CUDA event 计时包括提交间的 GPU 空闲，不含初始化、分配、CPU 校验和标签上传。
表内加速比先逐轮计算“原整 warp / 候选”，再取中位数；单位 ms 为每次完整集合 restriction 的中位数。没有包含 `ParallelCopyOsi`、MPI、regrid 或整个 average 阶段。

## Q19 结果

| 调用 ID / Box 数 | 原整 warp ms | 逐盒半 warp runtime | 批量整 warp runtime | 批量半 warp runtime |
|---|---:|---:|---:|---:|
| 1 / 4 | 0.24750 | 1.409× | 1.166× | 1.857× |
| 227 / 50 | 0.88033 | 0.943× | 4.848× | 7.791× |
| 33 / 286 | 5.04955 | 0.955× | 5.959× | 10.043× |
| 226 / 286 | 5.36781 | 0.971× | 3.433× | 5.736× |

大薄片占主导的早期调用能受益；碎盒集合中，逐盒半 warp runtime 的耗时分别增加 6.05%、4.72%、3.03%。
半 warp fixed 在这三个碎盒集合也没有收益，耗时增加 3.61%、2.24%、0.86%；仅固定整 warp 的 bool 则增加约 1.2%–1.4%。
单线程/父单元在碎盒集合的耗时增加约 111%–125%，8空间×4分组增加约 62%–70%。不能采用之前大立方盒上的排名。

## Q27 对照与数值结果

Q27 三个碎盒集合的逐盒半 warp runtime 耗时增加约 5.56%、4.45%、1.78%；批量整 warp 加速比为 4.700×、5.763×、3.349×，批量半 warp 为 7.246×、9.279×、5.225×。
这与[既有完整 Q27 求解器对照](2026-10-21-q27-half-warp-validation.md)中单独半 warp 无生产收益的方向一致；本次批量方案尚未集成完整求解器，不能外推 total/average 加速比。

64 个“模型×完整集合×方案”数值检查全部通过：最大分量误差 `2.220446049250313e-16`，最大密度/动量残差 `3.877106968808164e-16`。
完整结果包括逐轮范围；初始 4 盒集合存在个别较大计时波动，不作为碎盒优化结论依据。
作业 616398 是未加入批量方案的先行对照，也正常完成且数值检查通过；其节点/耗时单独保留，不与 616399 合并统计。

## 证据、复现与后续边界

- [完整比较表](../../runs/interface_retest_20261021/bench_616399/comparison.md)、[CSV](../../runs/interface_retest_20261021/bench_616399/summary.csv)、[JSON](../../runs/interface_retest_20261021/bench_616399/summary.json)。
- [冻结测试源码](../../runs/interface_retest_20261021/bench_616399/interface_benchmark.cu)、[采样选择脚本](../../runs/interface_retest_20261021/select_batches.py)、[汇总脚本](../../runs/interface_retest_20261021/summarize_interface.py)。
- [提交重放入口](../../runs/interface_retest_20261021/submit_benchmark.sh)在独立 `bench_<jobid>/` 中重建和运行，保留历史结果；[采集入口](../../runs/interface_retest_20261021/submit_capture.sh)保留构建/运行方法。各次运行含设备信息、源码、构建命令/日志、可执行文件、SHA256、原始计时与检查结果。
- `source_capture.sha256` 记录布局、插桩与重放源码、采集可执行文件的指纹。实验材料仅保留在所属 runs 目录，无正式源码、缓存状态或实验开关残留。

从算例目录执行：

```bash
dsub -s runs/interface_retest_20261021/submit_benchmark.sh
```

**下一步实施候选是按布局缓存跨盒任务并批量启动，再比较整/半 warp。** 正式集成需复用已有适用的标签/phase-cache 机制，明确源/目标存储与 regrid 的失效边界，并实际测量整个 average 阶段和数值回归；此次不提前加入生产替代路径。
