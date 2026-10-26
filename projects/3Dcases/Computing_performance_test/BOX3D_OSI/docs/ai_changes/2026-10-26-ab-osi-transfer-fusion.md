# AB/OSI 正常推进插值与界面平均融合

目的：让两种存储模式共享批量提交、平均线程映射和数值公式，使后续优化只维护一份实现。
范围仅为 BOX3D_OSI；基线为任务开始时干净的 `5302f34`，重新构建 DIM=2/3 后冻结。
正式输入、物理参数、Cycle2 时间调度、AMR 判据和通信后端未修改。

## 实现

- 正常推进统一调用 `FillDdfGhostFromCoarse(..., DdfLayout)`。两种布局分别用
  `ParallelCopy`/`ParallelCopyOsi` 装入 canonical stage，再共用物理边界延拓和
  `InterpolateDdfStageBatch<UseOsiAddress>` 的批量缩放/插值。
- 扩展现有 `InterpolationLevelCache.work` 和 `AverageDownLevelCache.block_tags`，
  两种模式都构建任务，不新增长期状态。任务保存几何/索引；每次执行取当前数组与 OSI phase，
  避免 A-B 双缓冲交换后绑定旧存储。沿用既有缓存失效和重建流程。
- 界面平均统一为 `AverageDownLayoutLevel<UseOsiAddress>`；复用并扩展 `load_ddf`
  的 const 读取，让同一内核只在读取地址上分派。结果先写 canonical interface buffer，
  再按目标布局搬运。为跨实现文件调用显式实例化两种 CPC 搬运模板。
- Q27 共用 16 lane/父单元、128 线程/block 的半 warp；Q9 共用 32 lane 合作处理一个父单元、
  256 线程/block。旧 OSI Q9 的标量调用在 32 线程组内重复处理同一父单元，本轮改为真正的整 warp 归约。
  Q19 保留单线程分支，但本轮没有切换格子模型验证 Q19。
- 删除已迁移调用者对应的 `FillOsiGhostFromCoarse`、`AverageDownOsiLevel`、
  `LaunchRestrictionWarps` 和重复的 A-B 缩放平均内核。
  重网格临时插值保留独立生命周期，完整 valid 平均保留原实现和覆盖范围。
  OSI 关闭细分计时时不因共同入口增加阶段同步；平均工作量计数现在覆盖两种布局。

选择扩展上述机制，是因为任务粒度、几何来源和缓存生命周期已经兼容；没有增加另一套执行器或访问器。

## 构建与数值验证

DIM=2/3 CUDA+MPI 均重新编译通过。CPU `dimension_kernels_test` 的地址/未定义行为检查通过，
增加了独立的先缩放再平均对照，覆盖有/无缩放和 phase 0/1/17。
容器环境运行命令为 `ASAN_OPTIONS=detect_leaks=0 bash tests/run_dimension_kernels_test.sh`，
未执行 LeakSanitizer。OSI 地址测试、Shell 语法、Python AST、文档路径和保留 CRLF 的 diff 检查通过。

主验证作业 **620242 SUCCEEDED**，whshare-agent-56，A100-PCIE-40GB，分配一张 GPU。
127 次应用退出均为 0；其中两个 MPI rank 的数值测试共享 GPU，不能当作两张 GPU 的验证。

- 独立 GPU 内核对照：DIM=2/3 各 80 个配置，共 160 个；父单元数 1/7/8/9/17，
  phase 0/1/17/100003，两种布局，有/无缩放。最大误差 `2.77556e-17`。
- 108 组数值运行：48 组阶段检查、48 组常规新旧程序回归、12 组三维三层动态回归。
  覆盖三种插值模式、1/2 rank、周期/非周期边界，以及实际新建、重建、清层过程。
- 阶段最大 Linf `1.776356839e-15`；active 最大 Linf `2.442490654e-15`。
  新旧同阶段 checkpoint 的 **233680896 个 valid DDF 分量**均有限，最大误差
  `2.4424906541753444e-15`。
- 程序末尾的 `final_state_check -> ComputeMacro -> AverageDownValid` 会在 checkpoint
  写出后刷新 covered 粗单元，因此不把其 live 全 valid 范数当作同阶段对照。
  分析器直接读取两端 checkpoint，裁剪 A-B ghost 后逐块比较 valid 数据。
  非周期物理域外 Stream 源诊断与修改前一致，不宣称全部 ghost 相等。

动态 fixture 最终清层后会重新建立较少层级；实际层级以生命周期日志和 checkpoint 为准，
不能把每份 step 192 checkpoint 都称作三层。
主报告：[620242/report.md](../../runs/temporary/ab_osi_transfer_fusion/job620242/report.md)，
逐层数值与原始计时：[report.json](../../runs/temporary/ab_osi_transfer_fusion/job620242/report.json)。

## 固定三层的追加核对与原有非有限状态

作业 **620246** 从主测 step 64 的有限三层 checkpoint 重启到 step 128，保持三层，关闭 regrid，
分别运行修改前后的 AB 和 OSI。四次应用均退出 0；原严格分析器拒绝非有限 valid 数据，
因此调度终态为 **FAILED**，不能写成作业成功。后续只读分区核对不改变程序或运行数据。

| 模式 | 层级 | active Linf | 有限 valid Linf | covered 非有限 DDF 分量 |
|---|---:|---:|---:|---:|
| AB | 0/1/2 | 0 | 0 | 全部为 0 |
| OSI | 0 | 0 | 0 | 305032 |
| OSI | 1 | 0 | 0 | 332962 |
| OSI | 2 | 0 | 0 | 0 |

按下一层 valid Box 粗化构造 covered 区；active 两端全部有限。
OSI 非有限值都在 covered 区，NaN/+Inf/-Inf 的类型与位置在新旧程序中逐点一致。
这证明本次范围内的分区等价，不证明 OSI 全 valid 正确；本轮没有修复该原有状态问题。
报告：[620246/report.md](../../runs/temporary/ab_osi_transfer_fusion/job620246/report.md)，
原严格分析器、失败日志、修正范围和新分析器均保留在该目录。

## 单卡性能

与主测同节点、同 step 64 checkpoint，64³ 基网格，三层固定布局，max_grid_size=32，Re=3200。
每次运行到 step 320；剔除首 64 步，比较 step 129–320，关闭输出、checkpoint、regrid、收敛与 OSI 细分计时。
两种模式分别四轮交替顺序；核对 L0/L1/L2 的调用次数及碰撞工作量一致。
下表为每轮新旧比值变化的中位数，负值表示耗时减少。

| 模式 | interp | average（含写回） | total |
|---|---:|---:|---:|
| AB | -59.791% | -73.350% | -16.566% |
| OSI | +0.056% | +1.145% | +0.046% |

AB 四轮 total 变化为 -16.476%/-16.525%/-16.607%/-16.699%；OSI 为
+0.061%/+0.025%/+0.035%/+0.057%，总耗时基本持平。此结果属于融合后的整体变化，
没有分别隔离批量提交与半 warp 的贡献，不能外推 128³ 生产布局或动态重网格性能。
`total` 包含统计窗口内推进与调度，排除初始化；本次窗口无重网格。

两卡申请 620240/620243 均在未启动时撤销：前者缩小资源后重提，后者最终无可用 GPU。
本轮没有两卡、跨节点或 device-direct 验证。

## 复现材料与保留范围

全部保存在 [runs/temporary/ab_osi_transfer_fusion](../../runs/temporary/ab_osi_transfer_fusion/README.md)：
基线/候选源码、输入、程序、SHA256、构建日志、实际命令、MPI 日志、checkpoint 和分析报告均保留。
程序指纹见 [executable.sha256](../../runs/temporary/ab_osi_transfer_fusion/executable.sha256)。

本次入口继续放在 `scripts/temporary/`，不是长期公共工具；它们是本实验的复现依赖，保留：
[提交与参数化追加检查](../../scripts/temporary/submit_transfer_fusion.sh)、
[逐值/计时分析](../../scripts/temporary/analyze_transfer_fusion.py)、
[独立 GPU 对照](../../scripts/temporary/transfer_restriction_gpu.cu)、
[独立对照构建](../../scripts/temporary/build_transfer_restriction_gpu.sh)。
没有删除用户工具或历史材料。
