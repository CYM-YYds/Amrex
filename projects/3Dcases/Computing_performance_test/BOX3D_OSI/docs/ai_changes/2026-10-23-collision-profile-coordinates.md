# 碰撞计数器分析与坐标复用实验

目的：核查真实L2碰撞的带宽/寄存器/指令瓶颈，并验证一个不改变BGK公式的OSI寻址候选。结果：计数器访问被系统权限阻止；坐标复用候选在固定真实布局上稳定慢约1.33%，不采纳生产改动。不能据此断言原碰撞已达性能上限或已经确认带宽瓶颈。

## Nsight Compute：未取得硬件计数器

[作业618060](../../runs/collision_profile_20261023/job618060/) 使用已验证的job617902程序、collision_batch_mode=0，从615364 step256000重启，固定网格、关闭输出。预热8个coarse steps，按每步1+2*10+4*33=153次碰撞发射，跳过1235次匹配发射，计划采集第9步首个L2推进的33个Fab。采集SpeedOfLight、Occupancy、MemoryWorkloadAnalysis、SchedulerStats、WarpStateStats、LaunchStats和InstructionStats。

Nsight Compute 2025.1 报 `ERR_NVGPUCTRPERM - The user does not have permission to access NVIDIA GPU Performance Counters`，最终退出1，没有ncu-rep或有效指标。应用本身完成不能替代采集成功。原始命令、日志、退出码和程序SHA256均保留。需要集群管理员授权性能计数器访问才能继续完成带宽/实际occupancy/stall分析；没有尝试修改驱动权限或提权采集。该作业不作为性能样本。

复现入口：[profile_collision.sh](../../scripts/temporary/profile_collision.sh)。此脚本是本次实验材料，保留日志和程序期间保留。

## 独立坐标复用候选

未依赖计数器做瓶颈定论，而是直接检验一个可独立验证的假设：复用Kernels.H已有的osi_cell_coordinates/load_ddf_cached/store_ddf_cached，为每个cell预计算九个坐标，再按速度方向选择，保持求和顺序、BGK公式、covered/interface规则、线程映射、通信及迁移不变。

[完整源码和构建日志](../../runs/collision_coordinates_20261023/candidate/) 从前次collision_batch实验的候选快照派生。保留该程序的批量实验路径，但本次始终collision_batch_mode=0，仅切换collision_cached_coordinates。原路径与候选在host侧分别发射kernel，避免把运行时分支放到同一GPU kernel。

[增量补丁](../../scripts/temporary/collision_coordinates_candidate.patch) 相对于collision_batch_20261023/candidate/src，使用git apply --unidiff-zero。复用[实验提交入口](../../scripts/temporary/test_ghost_collision.sh)，通过COLLISION_TEST=1、COLLISION_COORD_TEST=1选择实验；包装脚本在实验目录submit.sh。未修改正式src/config或覆盖正式可执行文件，用户已有修改未纳入提交。

编译成功。原OSI内核198寄存器，候选201寄存器，均无spill。cuobjdump静态SASS条目计数原路径3048、候选2608（约-14.4%）；IADD3 370→223、IMAD 403→244、ISETP.GE.AND 318→171。这是静态指令条目，不是动态执行条数或指令耗时。sass.txt和instruction_counts.json在候选目录；复现方式为cuobjdump --dump-sass app.ex，再把输出路径作为[审计脚本](../../scripts/temporary/analyze_ghost_collision.py)第二参数。

## 正确性和真实布局计时

job618062：12组64步阶段验证（插值0/1/2 × 非周期/全周期 × 1/2 ranks），基础32³、最高level2、BF8、max_grid_size=16、regrid_fixture=true、regrid_int=16，实际激活level2；19860条阶段检查最大linf=0，2376条迁移源检查的内部ghost mismatch均为0。双rank非周期物理域外来源仍有既有A-B差异，计数与前次关闭优化控制job617899逐组相同，不归入本次候选的数值等价范围。

真实615364 checkpoint推进32步至256032，各层valid/active DDF比较（84条valid_linf统计）全部为0，global linf/l1/l2=0。

性能：whshare-agent-192、A100-PCIE-40GB、单rank/GPU0；同程序、输入、checkpoint，BF32，固定原三层布局、regrid_int=-1；关闭输出/checkpoint/收敛/OSI细分计时。各自推进640步，以原/候选/候选/原顺序交替。每160步报告，剔除每次首窗口，每模式6个稳态窗口。四次应用正常结束。不是动态重网格、多rank性能或历史615364加速比。

| 每160步稳态均值 | 原路径 | 坐标复用 |
|---|---:|---:|
| perf total (s) | 5.791774 | 5.868551 |
| collide (s) | 3.931557 | 4.017931 |
| MLUPS_total | 1465.000 | 1445.833 |
| total窗口范围(s) | 5.788126–5.795606 | 5.866438–5.870900 |

总时间增加1.326%，碰撞时间增加2.197%。正反顺序均变慢，超过窗口波动。静态指令减少没有换来性能提升；不能据此证明是哪种硬件等待导致，寄存器数增加也不足以证明occupancy下降。

原始数据、指纹、命令及audit.json/summary.json见[作业618062](../../runs/collision_coordinates_20261023/job618062/)。本次未验证“降低寄存器上限/重读fq”方案，也未取得硬件瓶颈归因；后续应优先获得计数器权限，再决定寄存器与带宽之间的取舍，避免把198寄存器本身当成瓶颈证明。
