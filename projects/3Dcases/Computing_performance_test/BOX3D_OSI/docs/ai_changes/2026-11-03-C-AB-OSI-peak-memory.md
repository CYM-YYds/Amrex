# C 阈值 A-B / OSI 全程性能与峰值显存对比

目的：用户要求 C 阈值再比较双数组 A-B 与 OSI，并包括峰值显存。范围仅 BOX3D_OSI 实验与现有临时提交/采样工具，不修改求解器、用户inputs或已有结果。

复用a5_threshold_probe.sh：增加可选PROBE_STREAM_MODES=0 1，同一作业串行运行C_mode0和C_mode1；阈值组名与存储模式独立。增加PROBE_GPU_MEMORY_MS，应用启动前开始采样、退出后结束，EXIT trap清理本入口的采样进程；保留GPU/进程基线。复用summarize_restart_memory.py，扩展显式作业号、采样间隔和逐PID显存峰值汇总，并冻结分析器副本。无需复制新提交入口；默认不设置新参数时保留原行为。

采用C GIF作业同一frozen_20261029程序/输入，而非当前优化代码；SHA256为90ebe0140b8813bda64645988969987c9094cba408b1e77610403dcd190a29d4。无源码候选或构建配置变化，故不重新编译历史程序。两组均FLOAT、Re1000、128³、max_level2、阈值0.25/0.5/1/2、BF16、grid_eff0.7、regrid_int32，从静止运行到128000步/t1000。每3200步输出性能与速度/Q涡量场，checkpoint32000步全部保留，收敛达标不提前停止。

预检job624737：两模式128步正常完成、guard/rank退出0；采样得到GPU与进程记录及摘要。同一GPU设备采样峰值30685MiB、进程30672MiB，但Arena active used分别4398/2574MiB，allocated均30254MiB，说明默认内存池预留会掩盖活跃分配差异。此短测不能代替整次峰值，正式采样覆盖初始化、动态重构、推进、输出和退出。100ms只是采样峰值，可能漏掉短瞬态；设备memory.used不排除其他进程，需检查进程明细。

短测3层速度FAB逐字节一致、lev1/2 Q涡量一致；lev0 Q涡量有差异，uncovered valid Linf约0.007469721，两组均有限；不宣称DDF或长期逐值等价。正式对比应同时核对网格、每层launch_cells和调用数，按各自动态AMR运行报告，不能将网格工作量变化全归因于存储模式。

正式job624739已RUNNING，C_mode0先运行，C_mode1随后在同GPU运行。结果根runs/C_AB_OSI_t1000_peak_memory/；原C GIF作业624717保持独立。保留全部程序/源码/指纹/输入/日志/采样/场数据/检查点。当前无完整运行性能或峰值结论；固定顺序仅一轮不证明微小收益稳定。perf.total包含重构但不含初始化与场输出；需另列程序Total Time及guard.elapsed_seconds，Arena used/allocated与设备/进程采样峰值分别报告。

验证：两脚本bash/Python语法通过；GPU短测验证两种模式、采样和汇总实际可用；正式启动日志确认FLOAT、begin_step0、plot_int3200及C阈值/stream_mode0覆盖；git diff --check。复现命令、提交回执及启动状态位于正式实验根目录。
