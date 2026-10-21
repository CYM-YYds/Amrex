# 批量插值与界面平均正式合入

## 目的与范围

将已独立验证的正常 OSI 插值、CUDA 界面平均批量启动合入正式 BOX3D_OSI。碰撞、物理边界、通信写回、物理模型和生产输入保持原实现/配置；二维仍 D2Q9，三维仍 D3Q27。候选性能见此前插值及平均实验，本次重新验证两项同时启用。

## 主要改动与旧路径

- `FillOsiGhostFromCoarse` 直接批量缩放全部 staging Fab，再批量执行对应插值任务，三种模式复用既有数值函数。
- 正式实现按细层编号直接取得既有 staging/fine 布局生成的任务，不保留实验版的对象指针查找、运行开关或双算诊断。
- `InterpolateDdfStageToFine` 继续服务 canonical 与重网格临时 patch；它们的存储及生命周期不同，不扩大为正常 OSI 的持久任务缓存。
- CUDA `AverageDownOsiLevel` 每个原 block 对应一个缓存标签，合并所有本地 interface Box 启动；仍为 256 线程/block、32 lane/父单元，不改变归约算式、尾部线程或 `ParallelCopyOsi` 写回。
- CPU 平均继续原逐盒路径。共享 restriction 启动常量同时用于 A-B 原启动器和 OSI 缓存构建，避免 block 形状独立漂移。
- 两类新缓存由 `AmrCoreLBM` 按层持有，唯一来源为已有 staging/interface Box、fine Fab 归属及 `FabPhaseCache`。既有 `BuildInterpolationCache`/`BuildAverageCache` 建立，由 `ClearOsiTransferCaches` 在布局或存储改变前失效；执行时读取当前数组与 phase，不新增相位状态。

## 构建与复现

冻结目录：[`runs/batch_transfer_merge_20261021`](../../runs/batch_transfer_merge_20261021/)。其中包含合入前后源码、原二维/三维程序、正式新程序、输入和源码差异。

- 三维正式构建：`GEN_CCDB=0 MAKE_J=8 ./scripts/compile.sh --no-submit`，成功日志 `compile-20261021T210202.log`。
- 二维正式构建：`DIM=2 GEN_CCDB=0 MAKE_J=8 ./scripts/compile.sh --no-submit`，成功日志 `compile-20261021T210349.log`。
- AMReX 26.06、CUDA 12.8/GCC 11.3、FP64、sm_80，沿用正式构建脚本与配置。
- 合并验证与性能作业 `616460`，实际源码/程序/命令/节点信息在 `job616460/` 再冻结。

## 验证结果

24 组最终有效 DDF 对照（三种插值模式 × 1/2 rank × cavity/periodic × 二维/三维）全部通过，逐层 `valid_linf` 和 `active_linf` 均为 0。每组 64 步，64²/64³、最大 Box 32、max_level=2、regrid_int=32；周期组启用非均匀种子，均启用动态 regrid fixture。

另各在二维/三维执行一组双 rank、192 步生命周期回归；两种维度都实际记录新建、RemakeLevel、ClearLevel 和再次加密，最终所有有效单元误差均为 0。二维为 64²/最大 Box32，三维为32³/最大Box16，max_level=2。

26 组回归共同覆盖新缓存的建立与失效边界；不以构建成功或正常退出替代数值比较。此前两个独立实验的阶段逐值证据保留，不将它们冒充为本次合并程序的双算计数。

性能在 whshare-agent-96 的 A100-PCIE-40GB 上执行：正式 Re=3200、128³、最大Box128、max_level=2、regrid_int=32、interp_mode=0、非周期输入。单rank/GPU，每次512步、每256步报告，三轮依次原/新、新/原、原/新；关闭fixture、输出、收敛和双算诊断，关闭OSI嵌套细分计时。所有配对窗口各层碰撞calls/launch_cells、平均调用、缓存重建和边界工作量计数均相同。

| 完整512步指标 | 原秒数中位数 | 正式新程序秒数中位数 | 配对耗时减少中位数 | 三轮减少范围 |
|---|---:|---:|---:|---:|
| 插值阶段 | 10.333541 | 4.715409 | 54.383% | 54.173%～54.395% |
| 平均阶段 | 6.011485 | 3.453602 | 42.612% | 42.459%～42.911% |
| 求解器总耗时 | 50.790484 | 42.991937 | 15.370% | 15.316%～15.422% |

这些是两项同时启用的直接测量，不是把独立实验的加速百分比相加。`total`包含运行中regrid和建表，不含初始化；插值缓存构建中位数由0.051334秒增至0.054642秒，已包含在总耗时中。未修改的碰撞计时约增加1.22%，总收益已包含该变化，不以分项节省代替实际total。

仅看257～512步窗口，interp/average/total耗时分别减少57.584%/47.407%/18.602%；该划分不代表物理稳态。`average`包含写回，边界修复另计入boundary。`osi_* = 0`表示细分未测量；本次没有多GPU性能结论。

生产网格原/新程序另各执行512步，在同一写出阶段比较Header、Lattice和三级DDF数据/头文件：8个文件SHA256全部相同。没有混入ComputeMacro后对covered区的改写；文件一致性不替代长期物理/收敛验证。

全部60次运行正常退出。数据见[完整配对表](../../runs/batch_transfer_merge_20261021/job616460/comparison.md)、[自动检查结果](../../runs/batch_transfer_merge_20261021/job616460/summary.json)、[生产文件指纹](../../runs/batch_transfer_merge_20261021/job616460/checkpoint_sha_comparison.json)。源码、配置、脚本语法、差异空白及记录链接检查通过；正式源码和两个正式可执行文件与实际执行快照一致。

## 结果

正式源码和二维/三维可执行文件已替换为批量插值与批量界面平均；保留既有碰撞、边界、canonical/临时插值及CPU平均路径。没有实验开关或独立旧OSI正常推进选项。复现脚本、数据和日志保留；一次性准备脚本在冻结后删除。Q19后续候选在独立实验中测试，不改变此次生产模型默认值。
