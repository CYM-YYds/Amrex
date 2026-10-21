# 界面平均批量启动：完整求解器实验

## 目的与范围

在正式 BOX3D_OSI 的独立快照中验证：保持一个 warp 处理一个粗单元、每 block 256 线程、原有缩放和 OSI restriction 数学操作，只将多个 interface Box 的 block 合并到一次启动，是否能改善完整平均阶段和求解器耗时。主推进物理边界已使用任务批量启动，本次不修改边界、插值、碰撞或平均通信写回。

正式 `src/`、输入和可执行文件不变。本次仅提交实验记录；候选实现、原路径、构建命令、脚本、日志和数据保存在独立实验目录。此实验使用正式三维 Q=27，不作为 Q=19 或二维的验证。

## 实现与状态生命周期

- 唯一原型在 `runs/average_batch_20261021/src/`，原始源码在同目录 `baseline_src/`；`experiment.diff` 记录差异。
- 原路径已经有稀疏 `average_interface_buffer` 和对应 fine Fab 索引。批量路径复用这些所有者映射、已有 `FabPhaseCache` 以及 restriction 数学内核；没有修改 `ParallelCopyOsi`。
- 新增标签按原 Box 的每个 block 保存几何、首个父单元、本地输入/输出索引和 phase 表索引。每 block 仍处理最多 8 个父单元，原有尾部空线程不变。
- 标签只在既有 `BuildAverageCache` 中按布局生成并上传，由 `AmrCoreLBM` 分层持有；既有 `ClearOsiTransferCaches` 在布局/存储改变前失效。执行时读取当前数组和共享 phase 表，不缓存旧相位值。
- `verification.batch_average` 和 `verification.batch_average_compare` 仅存在于实验程序，参数初始化时读取一次。关闭批量时沿用原逐盒路径，CPU 分支也沿用原路径。
- 验证开启时，实际输入分别运行批量与逐盒实现，检查全部目标 interface 单元的全部 Q 分量；另外独立检查相反的缩放开关。非有限值单独报错，不以 NaN 差值参与最大误差归约。

## 构建与实验依据

实验目录：[`../../runs/average_batch_20261021/`](../../runs/average_batch_20261021/)。

- 源码基线和原程序来源见 `git_head.txt`、`formal_before.sha256.json`、`baseline.ex`；原程序 SHA 与上一轮插值/碰撞实验基线相同。
- 构建作业 `616422`：全部 9 个算例翻译单元重编译以保持类布局 ABI 一致；仅复用不变的 AMReX 对象。CUDA 12.8、GCC 11.3、HMPI 1.2、AMReX 26.06、FP64、sm_80；完整命令和日志保留。
- 提交 `616425` 因脚本无执行权限而未启动求解器，修正权限后重新提交，不将该失败提交纳入结果。
- 完整验证/性能作业 `616426`，源码、可执行文件、输入、覆盖参数和提交脚本再冻结到 `job616426/`。运行节点及 GPU 信息分别见 `host.txt`、`gpu.txt`。

## 验证与性能结果

验证矩阵：3 个插值模式 × 1/2 个 rank × 非周期 cavity/全周期，共 12 组；64³、最大 Box 32、max_level=2、regrid_int=32，启用动态 regrid fixture，运行 64 步。周期组启用非均匀种子。额外关闭批量作控制，确认新可执行文件的原路径与正式原程序相同。

- 840 次同源 interface 逐值检查全部通过，缩放开关 0/1 均覆盖，最大误差及非有限值计数均为 0。
- 12 组各层最终有效 DDF（含 covered 有效单元）与原程序 checkpoint 对照最大误差均为 0；关闭批量的控制组也为 0。
- 1/2 rank 均使用已配置的 host staging；本次不验证 CUDA-aware MPI。

性能使用同一实验可执行文件，仅切换批量开关。三轮顺序为 off/on、on/off、off/on，在 whshare-agent-92 的 A100-PCIE-40GB 上执行，每次单 rank/GPU、512 步、report_int=256。使用正式 Re=3200、128³、最大 Box 128、max_level=2、regrid_int=32、interp_mode=0、非周期输入；关闭 fixture、输出、收敛和逐值诊断，保留主阶段计时，关闭 OSI 嵌套细分计时。

| 512 步指标 | 原路径秒数中位数 | 批量秒数中位数 | 配对耗时减少中位数 | 三轮范围 |
|---|---:|---:|---:|---:|
| 完整平均阶段 | 6.100671 | 3.445391 | 43.524% | 43.377%～43.558% |
| 求解器总耗时 | 51.126796 | 48.694103 | 4.758% | 4.750%～4.936% |

原/新完整平均阶段耗时之比约 1.77。这里 `average` 含 restriction 计算及本地/MPI 写回，不是纯计算回放；边界修复另计入 `boundary`。本次单 rank 性能组没有远程 MPI 传输。`total` 包含 regrid 及运行中的新标签构建，不含初始化；关闭细分计时后的 `osi_* = 0` 表示未测量。

全部配对窗口的各层碰撞调用/处理单元数、平均调用数、regrid 和边界工作量计数相同。当前 OSI 的 `average_parent_cells`/`average_scale_cells` 恒为 0，不能将这两个字段当成实际 restriction 单元统计。

另运行原程序和批量候选各 512 步，在相同 checkpoint 写出阶段比较全部 8 个文件（Header、Lattice 及三个层的 DDF 数据/头文件）：SHA256 全部相同。该项比较不混入后续 ComputeMacro/AverageDownValid 对 covered 单元的改写，也不声称 inactive ghost 全部有限。

仅看第 257～512 步窗口，平均耗时减少中位数 48.272%，总耗时减少 5.798%；这是排除最初窗口后的统计，不是物理稳态判定。

33 次运行全部正常退出。自动汇总及逐轮数据见 [`comparison.md`](../../runs/average_batch_20261021/job616426/comparison.md)、[`summary.json`](../../runs/average_batch_20261021/job616426/summary.json)；同阶段完整文件比较见 [`checkpoint_sha_comparison.json`](../../runs/average_batch_20261021/job616426/checkpoint_sha_comparison.json)。复现入口见 [`experiment_notes.md`](../../runs/average_batch_20261021/experiment_notes.md)。

## 判断与限制

当前 Q=27 的真实动态 AMR 算例支持采用“保留 warp 算法、合并 interface Box 启动”：完整平均阶段约 1.77 倍、完整求解器耗时减少约 4.76%，三轮配对收益一致。收益对应整个启动组织的变化；没有 GPU profiler 证据，不将它全部归因于 CPU launch 开销。

本次未叠加半 warp、批量插值或碰撞，不把单项加速简单相加。两 rank 仅验证数值，尚无多 GPU 性能、Q=19、二维、长期稳定性或物理收敛结论。标签额外占用与 block 数成正比的显存；未单独测峰值显存或 BuildAverageCache 耗时，运行中的构建成本已包含在 total。

正式源码、配置及可执行文件的 SHA 与实验前全部一致，核对结果见 `formal_unchanged.json`。本次仍保留原生产路径，候选只存在于实验快照。新增一次性准备脚本在源码/差异及完整复现入口冻结后删除；运行数据、日志和唯一复现材料保留。
