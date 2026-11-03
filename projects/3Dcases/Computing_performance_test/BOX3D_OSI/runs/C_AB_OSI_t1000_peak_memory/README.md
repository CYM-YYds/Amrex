# C 阈值 canonical A-B / OSI 全程性能与峰值显存（正式实验）

同一作业、同节点单GPU串行C_mode0→C_mode1，mode0为双数组A-B，mode1为OSI，均从静止运行128000粗步至t1000。C阈值0.25/0.5/1/2、FLOAT、Re1000、128³、max_level2、max_grid_size128/256/512、BF16、grid_eff0.7、regrid_int32；沿用原C frozen_20261029的程序和输入（非当前优化源码）。程序SHA256 90ebe0140b8813bda64645988969987c9094cba408b1e77610403dcd190a29d4。

两模式均每3200步输出速度与Q/涡量场和性能窗口，checkpoint每32000步全部保留，正式收敛达标也继续到终点。保留全部输入、程序、源码、指纹、命令、日志、采样、场数据及checkpoint。原C GIF作业624717独立保留；此次OSI为重新测量，不混用跨节点历史性能。

每个模式应用启动前开启100ms nominal nvidia-smi采样，覆盖初始化、推进、动态重构、场/检查点输出直至退出。gpu-memory.csv为设备整体memory.used（MiB），gpu-process-memory.csv为逐GPU/PID进程memory；gpu-before.txt和processes-before.csv记录基线/干扰。每个模式退出后生成gpu-memory-summary.json，并保留应用Arena used/allocated高水位于run.log。默认Arena预留约30GiB，故设备峰值不等于活跃分配；采样峰值可能漏掉短于采样间隔的瞬时峰值。

比较时间应分别汇总perf(s).solv、total（含重构但不含初始化/流场输出）、日志Total Time及guard.elapsed_seconds（进程启动至退出含I/O和MPI）。100ms监控开销对两模式一致，但不能声称无扰动。仅一轮mode0→mode1，不是交替重复统计，微小差异不作稳定因果结论；各自动态AMR网格可能不同，须同时比较每层网格和launch_cells/调用数。

预检job624737：128步两模式退出0、显存链路成功、三层速度FAB逐字节一致，lev1/2 Q涡量一致，lev0 Q涡量存在差异（含active区域），故不宣称长期逐值等价，正式结果按独立动态AMR运行解释。详细证据见runs/temporary/C_AB_OSI_memory_preflight/job624737/preflight_check.json。
