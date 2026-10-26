# 完整 valid 平均与重网格插值进一步融合

目的：在 `96aa0cb` 已统一正常推进的基础上，让完整 valid 平均、重网格临时插值也使用共同计算层。
范围仅 BOX3D_OSI；正式 inputs、物理模型、时间调度、AMR 判据和通信后端未改。

## 主要改动与状态约束

- 完整 valid 平均统一为 `AverageDownValidLevel(..., DdfLayout)`，与界面平均共用
  `BuildRestrictionWork`、`RestrictDdfToCoarse` 和格子专用数值核。
  全覆盖任务使用 coarsen(fine valid)，界面任务继续使用稀疏 interface Box，覆盖范围分别维护。
- 删除 `AverageDownOsiValidLevel` 及其细层/粗层完整 canonical 解码工作区。
  结果只写入 fine valid 所对应的 coarse 单元，按布局搬运，保留 uncovered 粗单元。
- AB 原实现会把缩放后的全部 fine valid 留在 `f_new`，后续交换会保留这些状态。
  共用内核的编译期 `StoreScaledFine` 输出保留此行为，包含关闭缩放时的复制；不写 ghost。
  OSI 及界面平均不启用此输出。寻址、辅助写入的差异没有形成另一套归约或缩放公式。
- 重网格初始化与正常插值共用 `BuildInterpolationWork` 和 `InterpolateDdfStageBatch`，
  删除逐盒执行器 `InterpolateDdfStageToFine`。模式 0 按 fine cell，模式 1/2 按 parent 并裁剪到请求区。
  初始化目标为 canonical 或 phase=0 OSI；父层 OSI 仍按显式 source phase 搬运。
- 任务格式、建表与执行复用，生命周期保持分开：正常任务归层缓存，完整平均/初始化任务为局部对象。
  GPU/MPI 完成后再释放任务、搬运计划和暂存区；不新增持久派生状态。

新建共同建表/执行函数是为了消除真实重复的布局映射、批量提交及数值职责，
不合并语义不同的插值标签与平均标签。保留既有边界修复和重网格搬运路径。

## 验证

基线采用上一轮冻结的 `96aa0cb` 程序，复用前逐项核对源码与程序 SHA256。
本轮 DIM=2/3 CUDA+MPI 均重新构建通过，CPU 地址/未定义行为检查通过
（`ASAN_OPTIONS=detect_leaks=0`，不包含 LeakSanitizer）。Shell、Python AST、链接路径和 CRLF diff 检查通过。

主作业 **620330 SUCCEEDED**：whshare-agent-63、单张 A100-PCIE-40GB。
两个 MPI rank 共享该 GPU；本轮没有两卡、跨节点或 device-direct 验证。

- 独立 GPU 内核对照共 **320 个配置**：二维/三维、两种布局、有/无缩放、普通/保留细层写入、
  多种绕环 phase 和尾部父单元组；最大误差 `4.16334e-17`。细层写入范围外 sentinel 保持不变。
- 修复直接依赖的初始化测试入口，使其从当前生产源码提取类型、建表和执行函数。
  1/2 rank 各 **36 个配置**，覆盖三种插值、非周期/全周期/混合周期、AB/OSI、phase 0/17/41。
  独立参考公式对照完整 patch 和不完整父单元 patch，最大误差 `2.220446049e-16`；
  fine ghost 和请求区外 valid 不改写。覆盖缺口负例以 exit 6 在预期定位处终止。
- **108 组数值运行**覆盖三种插值、1/2 rank、周期/非周期及实际新建、重建、清层。
  AB/OSI 锁步阶段 Linf 为 **0**；同阶段 checkpoint 的 **233680896 个 valid DDF 分量**均有限，
  最大误差 `2.275957200481571e-15`，active 最大误差 `2.220446049250313e-15`。
- 原有非周期物理域外 Stream 源诊断与基线一致，不据此宣称全部 ghost 相等。
  final_state_check 会在写出 checkpoint 后刷新 covered 值，分析仍比较保存的同阶段 checkpoint。

主作业 130 次应用中 129 次退出 0，另一次为上述预期失败负例。
报告：[620330/report.md](../../runs/temporary/ab_osi_transfer_complete/job620330/report.md)，
逐层数值、原始计时及工作量核对：[report.json](../../runs/temporary/ab_osi_transfer_complete/job620330/report.json)。

固定三层追加作业 **620331 SUCCEEDED**，四次应用退出 0，从主作业的同一 step 64 checkpoint 重启至 128，关闭 regrid：
AB 三层 valid 均有限且新旧差异为 0；OSI 三层 active 及有限 valid 差异为 0。
OSI L0/L1 仍分别有 305032/332962 个 covered 非有限分量，NaN/+Inf/-Inf 类型与位置新旧逐点一致；
L2 全 valid 有限。这里通过的是分区等价核对，严格全 valid 有限性验收仍为 false，没有修复原有 covered 状态问题。
报告：[620331/report.md](../../runs/temporary/ab_osi_transfer_complete/job620331/report.md)。

## 动态重网格计时

基线为 `96aa0cb`；同节点同 checkpoint，64³ 基网格、最高 level 2、max_grid_size=32、Re=3200。
每 32 步使用受控 regrid_fixture，四轮交替顺序；剔除 step 65–128，统计 129–320。
关闭输出、checkpoint、收敛和 OSI 细分计时；L0/L1/L2 碰撞调用及工作量一致。

| 模式 | interp | interface average | RefineMesh 调用 | total |
|---|---:|---:|---:|---:|
| AB | -0.112% | -1.003% | +2.377% | +0.120% |
| OSI | +0.100% | -1.747% | -12.988% | -0.100% |

表中为四轮配对变化的中位数。AB 四轮 total 为 +0.541%/+0.210%/-0.082%/+0.029%；
OSI 为 +0.315%/-0.515%/+0.405%/-0.534%，没有稳定整体加速。
`RefineMesh` 区间包含完整平均、重网格、边界修复和缓存重建，不等于单个 kernel。
这不是生产 128³ 或真实加密判据下的性能结论，也不与上一轮固定布局结果叠加计算收益。

## 保留材料

[实验目录](../../runs/temporary/ab_osi_transfer_complete/README.md) 为本任务的 AI 短测目录。
保留基线/候选源码、输入、程序、SHA256、编译日志、提取后的生产测试函数、命令、MPI 日志、checkpoint 和报告。
复用并参数化原 [提交入口](../../scripts/temporary/submit_transfer_fusion.sh) 的 `FUSION_ROOT`、
`FUSION_PERF_REGRID_INT`、`FUSION_PERF_FIXTURE`，没有复制新入口。
程序指纹见 [executable.sha256](../../runs/temporary/ab_osi_transfer_complete/executable.sha256)。
