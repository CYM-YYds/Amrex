# 插值与碰撞批量启动：计划、完整求解器验证及性能结果

目的：验证跨 Box 批量启动能否改善插值、碰撞阶段及总推进时间。
范围：正式 BOX3D_OSI 当前三维 Q27、OSI 生产模式；只在独立源码快照中实现原型，正式源码、生产配置与主可执行文件不变。未测试 Q19/二维完整求解器，不把结果外推到这些模型。

## 结论

**本轮推荐只采用批量插值作为后续落地候选，碰撞维持原逐 Fab 启动。**
同作业三轮配对的完整512步中，批量插值使 interp 阶段耗时降低 **54.685%**，total 降低 **10.743%**（三轮范围10.660%–10.765%）。
只批量碰撞则使 collide 耗时增加 **8.943%**，total 增加 **4.559%**；两者同时启用的总收益被碰撞开销抵消一部分。
这些百分比是耗时减少，不是吞吐率增加百分比。

## 计划与原型实现

1. 先冻结原源码、输入、可执行文件和生产编译/链接命令，全部9个算例翻译单元重编译，避免新增成员后混用旧类布局；不变的 AMReX 对象复用。
2. 新增只存在于实验程序的 verification.batch_interpolation、batch_collision、batch_compare；构造时读取一次，验证双算与性能运行分离。
3. 复用已有 TagVector、FabPhaseCache，批量任务在布局构造/重构时建立；建表与上传成本计入实际推进/regrid，不在每步重建后从计时中隐藏。
4. 在同源逐值和最终 checkpoint 回归通过后，用同一实验可执行文件交替运行原启动、只插值、只碰撞、两者同时启用四配置。

插值沿 `FillOsiGhostFromCoarse → InterpolateDdfStageToFine` 执行；同一 stream 先批量缩放所有 coarse stage Fab，再批量处理全部 fine work box，三种 interp_mode 均调用原有数值函数。
正常推进缓存由既有 coarse stage、fine_work_boxes、fine_indices 派生，匹配 stage/fine 对象后才走批量路径；重构临时 patch 与 canonical 路径继续使用原实现，未扩大为所有插值入口。

碰撞复用 BoundaryWorkTag 的区域、Fab 几何、本地索引、phase 索引；执行时取得当前状态与掩码数组。
只批量 OSI、n=nghost=2 的正常推进，其他情况保持旧路径；covered interior 跳过、interface 例外、周期/非周期域裁剪、碰撞数值函数和阶段末同步不变。

新状态由 AmrCoreLBM 独占：InterpolationBatchCache 保存对象识别指针及派生任务；CollisionBatchCache 保存几何任务和派生 launch_cells。
唯一来源是现有 stage、工作盒、BoxArray/DM、FabPhaseCache；相位仍只来自 osi_phase，不新增时间状态。
缓存纳入现有 ClearOsiTransferCaches，在 regrid、ClearLevel、stage/状态存储替换前失效，随后由既有布局重建过程重建。

[详细计划/实现边界](../../runs/interp_collision_batch_20261021/experiment_notes.md)、[实际性能版本完整源码差异](../../runs/interp_collision_batch_20261021/experiment.diff)、[实际性能版本源码](../../runs/interp_collision_batch_20261021/job616410/src/)。

## 数值验证

主验证/性能作业 **616410**，基线提交 dbf44cd，实验前工作区干净。
验证为三种插值模式 × 1/2 rank × 非周期腔流/全周期，共12组：64³、max_grid_size=32、max_level=2、regrid_int=32，现有 regrid_fixture，推进64步；周期组启用非均匀 seed pattern。
每组先用原正式版本生成 checkpoint，再用两个批量开关均开的实验版本做同源逐值检查并与原 checkpoint 比较所有 valid DDF（包含 covered）。
额外用实验开关全关的版本对比原正式版本，确认类布局改动/重编译后控制组一致。

- 插值同源检查 **420次**：原 stage 复制后独立执行原缩放与原逐盒插值；比较缩放后的整个 stage 和实际写入 fine target 的所有 q，最大 scale/interp 误差均为 **0**，检查非有限值。
- 碰撞同源检查 **1608次**：比较原 Collide 的实际 launch box、active mask 与 interface 例外集合；先复制同一 raw 输入，再分别运行批量/原逐盒数值函数。有限分量最大误差 **0**，valid 非有限值 **0**，参考双方有限性/NaN/Inf 类型不一致 **0**。
- 12组最终 all-valid checkpoint 比较及开关全关控制组，最大误差均为 **0**。
- 两项阶段误差和 checkpoint 通过门槛均为2e-14；未以编译、正常退出或范数趋势代替逐值检查。

碰撞 ghost 不能和 valid 混为一个有限性判据：首次初始化只填 valid，通信前部分内部/周期 ghost 在原路径与批量路径中同为 NaN。
初次作业616406的粗检查拒绝所有非有限值而中止；没有发现有限分量误差，不纳入性能统计。
后续将 valid、内部 ghost、周期 ghost 及双方非有限状态不一致分开，valid 仍要求有限，任何一方新增 NaN/Inf 或类型不一致仍失败。

另用作业 **616413** 复核 ghost 分类（1 rank、腔流/周期、64步）：双方相同的非有限 q 数每格最多27；valid 非有限与非有限状态不一致均为0，最终 checkpoint 误差仍为0。
主验证版本诊断汇总误把 `MultiFab::sum(comp,bool local)` 的第二参数当成 ghost 宽度，因此该版本 ghost 计数不采用；616413改用覆盖 ghost 的 norm0 取得每格最大 q 数。
该修正仅改诊断统计，数值路径和用于拒绝不一致的 norm0 检查不变。没有借此声称所有 ghost 有限或修复历史 ghost 生命周期。

[主验证记录](../../runs/interp_collision_batch_20261021/job616410/validation_checks.json)、[主结果JSON](../../runs/interp_collision_batch_20261021/job616410/summary.json)、[ghost复核](../../runs/interp_collision_batch_20261021/ghost_616413/)。

## 完整求解器性能

主作业节点 whshare-agent-190，NVIDIA A100-PCIE-40GB；两 GPU 用于 MPI 正确性验证，性能部分统一单 rank/单 GPU。
生产输入 Re=3200、128³、max_level=2、max_grid_size=128、regrid_int=32、interp_mode=0、非周期；不启用 fixture。
AMReX26.06、CUDA12.8/GCC11.3、SM80、C++20、FP64、RDC、生产 `-O3 --use_fast_math` 构建；仅切换实验启动开关。
关闭 PlotFile、checkpoint、收敛停止、验证双算和 OSI 嵌套细分计时；MPI传输设同步host staging，单rank没有远程传输，不据此宣称多rank性能收益。

每轮四配置按正/反顺序交替，共三轮；每次512步、每256步输出一个窗口。
配对窗口的每层碰撞calls、launch_cells及已输出的插值/平均工作量计数一致。
下表按每轮相对原启动的耗时变化先配对，再取中位数；负号表示更快，正号表示更慢。

| 方案 | interp耗时变化 | collide耗时变化 | total耗时变化 | total三轮范围 |
|---|---:|---:|---:|---:|
| 只批量插值 | -54.685% | +0.567% | **-10.743%** | -10.765%～-10.660% |
| 只批量碰撞 | -0.092% | +8.943% | **+4.559%** | +4.384%～+4.623% |
| 两者同时启用 | -54.548% | +10.104% | **-5.930%** | -5.990%～-5.816% |

完整512步的total中位数：原启动50.813974秒，只插值45.344103秒，只碰撞53.100563秒，两者47.800455秒。
缓存构建并非免费：只插值组 interp_cache_build 中位数由0.050692秒增至0.054575秒；上述total已包含建表/regrid的实际成本。

排除第一个256步窗口后（只统计257–512步），只插值的interp减少57.893%、total减少12.905%；只碰撞total增加4.220%，两者同时启用total减少8.356%。
该窗口划分只排除启动窗口，不代表达到物理稳态或正式收敛。
`interp` 是整个 FillGhostLevel 阶段，包含原有复制、边界填充等，不是单纯插值GPU kernel；`total` 包含重构、排除初始化。
`osi_* = 0` 表示细分计时关闭、未测量，不表示没有通信成本。

碰撞减少启动没有带来收益，不能把平均碎盒重放中的倍数套用到整Fab体区域碰撞。
TagVector 路径引入任务查找、索引/数组间接访问等成本；这些可能抵消减少启动的收益，但没有 profiler 分解，不能把退化确切归因于某一项。
编译资源和完整函数名见[资源报告](../../runs/interp_collision_batch_20261021/job616410/resources.txt)，静态寄存器数不能代替动态瓶颈证据。

[完整性能表](../../runs/interp_collision_batch_20261021/job616410/comparison.md)、[逐对/逐窗口数据](../../runs/interp_collision_batch_20261021/job616410/summary.json)、[各组退出状态](../../runs/interp_collision_batch_20261021/job616410/status.tsv)。

## 生产配置最终DDF复核

补充作业 **616415**：原正式程序与推荐的只批量插值原型，均按生产网格和动态 AMR 推进512步、写出同一步 checkpoint，并执行现有 FinalState 检查。
两份 checkpoint 的 Header、Lattice 及三级 DDF 数据/头文件共 **8个文件 SHA256 全部一致**，包含 covered 区的完整 valid 存储。[逐文件结果](../../runs/interp_collision_batch_20261021/production_616415/checkpoint_sha_comparison.json)。
该存储一致性与上述阶段逐值证据共同支持当前配置的数值等价；不能仅凭文件指纹宣称物理正确、长期稳定或所有非活动槽位均有限。

初始补充脚本同时开启 final_state_check 和内存状态对 checkpoint 的比较，曾输出 active误差0、covered非零及部分全valid范数NaN。
已定位为本次诊断阶段选择错误：checkpoint 在循环内写出，final_state_check 随后调用 ComputeMacro→AverageDownValid 改写covered DDF，再与此前 checkpoint 比较，比较对象不在同一阶段。
这些跨阶段范数不用于判定批量插值差异；原始日志保留。复现入口已改为两份同阶段 checkpoint 的指纹比较，同时保留 FinalState 检查，避免再次混比。
[原始补充脚本](../../runs/interp_collision_batch_20261021/production_616415/submit_original.sh)、[同阶段比较脚本](../../runs/interp_collision_batch_20261021/compare_checkpoints.py)。
该补充作业在另一节点执行，只用于数值回归，不混入616410性能统计。

## 复现与边界

- [冻结原源码](../../runs/interp_collision_batch_20261021/baseline_src/)、[基线提交](../../runs/interp_collision_batch_20261021/git_head.txt)、[构建命令](../../runs/interp_collision_batch_20261021/build_commands.json)。
- [独立构建入口](../../runs/interp_collision_batch_20261021/submit_build.sh)、[验证/性能入口](../../runs/interp_collision_batch_20261021/submit_validation.sh)、[汇总脚本](../../runs/interp_collision_batch_20261021/summarize.py)、[ghost复核入口](../../runs/interp_collision_batch_20261021/submit_ghost_check.sh)。
- [生产配置最终回归入口](../../runs/interp_collision_batch_20261021/submit_production_check.sh)。每个运行使用独立job目录，保存可执行文件、输入、实际命令和原始日志；37个主运行全部正常退出。
- 性能版本源码/二进制在job616410冻结；顶层src/experiment.ex包含后续仅诊断ghost统计修正，对应ghost_616413，不混用二者的源码指纹。

从算例目录可用上述提交入口重建/重跑；`summarize.py <job目录>` 汇总同一作业内的配对结果，不合并不同节点耗时。
性能只测默认interp_mode=0，模式1/2仅完成数值验证；多rank仅验证正确性。没有长期稳定性、收敛、Q19或二维性能结论。
本轮完成原型与证据，不把实验开关或替代内核留在正式源码。后续若正式落地，优先选择批量插值，并按正式支持范围补充构建与数值回归。
