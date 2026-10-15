# OSI 嵌套细分计时改为可选

## 目的与实现

减少 OSI 通信及缓存搬运中仅为细分统计引入的 GPU 同步。
复用 `RuntimeParams` 和 `ScopedPerfTimer`，不另建计时状态或 GPU event 队列。
新增 `performance.osi_detail_timing`，最终默认 1，延续原有 OSI 细分计时；
设为 0 可关闭这些计时同步。实测没有一致的整体收益，因此不默认关闭统计。
`ScopedPerfTimer` 关闭时不读取时钟、不同步 GPU、不累加耗时，其他调用默认保持原行为。

- `FillBoundaryOsi()` 的总细分、pack/kernel/D2H/wait/H2D/unpack 嵌套计时受开关控制。
  staging 手动累加字段也在关闭时停止更新；用于搬运完成判定的控制流程不变。
- 缓存 `ParallelCopyOsi()` 的总细分计时关闭时，传给 `OSI::parallel_copy_mpi()` 的
  `CopyTiming` 清空，去掉其仅用于 wait 统计的同步。
- 不改任何显式数据同步：pack 完成后再让 staging stream/MPI 读取、stream 1 的
  D2H/H2D 完成等待、平均写回、MPI unpack 与缓冲释放等原有同步全部保留。
  单 rank 插值暂存随后的 MFIter 在切换多 stream 前有 AMReX 26.06 的 pre-sync；
  单 stream/单 Fab 在同一 stream 顺序消费。
- `comm/interp/average` 等主阶段计时不变。关闭时所有 OSI 细分秒数字段为零，
  表示未测量；新增 `perf(s): osi_detail_timing=0/1` 标明口径。
  本次不关闭其他阶段计时，也不改变全局 TinyProfiler 配置。

范围限于 BOX3D_OSI。用户已有函数重命名及其他文档改动不纳入提交。
基线 app 为上一轮恢复范围检查后重新编译的版本；候选源码、app、输入、指纹、
命令、日志及分析脚本保存在[实验目录](../../runs/osi_timing_20261015/)。

## 数值验证

候选 CUDA+MPI 构建通过。作业 `613485` 为 `SUCCEEDED`，九组各 64 步：
三种插值单 rank 动态 AMR、混合周期/全周期固定单层、两 rank 动态 AMR 的
分块流水/同步 staging/异步 staging，以及两 rank 开启细分计时。
所有组的逐步逐层检查通过；前后阶段/数值记录相同，MPI 比较保留记录重复次数、
允许输出交错。每个插值组 1484 条记录和 7 个 checkpoint 文件相同；
两个周期组各 768 条和 3 个文件相同；四个 MPI 组各 1487 条和 10 个文件相同。
关闭时所有 OSI 细分字段为零，开启时 fillboundary/pack-kernel 有正计时，日志标志正确。

该矩阵使用候选初始默认 0，`mpi_detail` 显式设为 1。
最终默认改为 1 后重新构建，作业 `613490` 为 `SUCCEEDED`：两 rank 的
开关 0/1 各 64 步，分别与原基线的 1487 条阶段/数值记录、10 个 checkpoint 文件相同，
阶段完整性和日志开关/计时字段检查通过。最终源码与性能候选仅有默认值差异，
最终 app 和源码快照另存为实验目录中的 `final.ex`、`final_src/`。
checkpoint 相同仅指保存的 valid 存储；既有 physical-ghost source mismatch 保持原状，
不声称全部生产 ghost 一致。多节点、device-direct、长期及运动 IBM 不在验证范围。

## 配对性能

两项作业均为 `SUCCEEDED`，A100 PCIe 40 GB；比较旧 app 的原计时与候选关闭细分计时。
`613486` 位于 `whshare-agent-63`，1 rank，Box=32/128 各五对；
`613487` 位于 `whshare-agent-89`，2 ranks/2 GPUs，Box=32 五对，host-staging 分块流水。
128³、Re=1000、最多三级、regrid=32，交替运行顺序；从同一第 96 步 checkpoint
推进至 608，丢弃第 128 步窗口，汇总第 192–576 步七个 64 步窗口，共 448 步。
百分比为每对 `(旧计时耗时−关闭细分后耗时)/旧计时耗时` 的中位数。
双 rank 对每个窗口的同一指标取较慢 rank 耗时后求和，不将两个 rank 耗时相加。

| ranks / 最大 Box | comm 降低 | interp 降低 | average 降低 | total 降低 | total 更快 |
|---|---:|---:|---:|---:|---:|
| 1 / 32 | 0.000% | 0.354% | 0.106% | 0.017% | 3/5 |
| 1 / 128 | 0.321% | 0.357% | 0.219% | 0.061% | 4/5 |
| 2 / 32 | −1.167% | −0.353% | 0.420% | −0.542% | 2/5 |

单 rank total 变化范围分别为 −0.120%–0.145% 和 −0.002%–0.083%；
双 rank 为 −2.692%–0.428%，波动较大。全部配对的碰撞调用和格点数相同，
边界逻辑任务数相同；没有以减少数值工作量换取速度。
关闭细分确实减少计时同步，但未证明稳定的整体加速，因此最终默认保持原统计。
仍保留开关，供明确不需要细分数据的受控实验使用。

完整数据见[单 rank](../../runs/osi_timing_20261015/job613486/analysis.json)、
[双 rank](../../runs/osi_timing_20261015/job613487/analysis.json)；
复查使用仓库根 `.venv/bin/python` 调用实验目录中的 `analyze_perf.py job613486`、
`analyze_perf2.py job613487`，数值汇总使用 `analyze_correctness.py job613485`。
