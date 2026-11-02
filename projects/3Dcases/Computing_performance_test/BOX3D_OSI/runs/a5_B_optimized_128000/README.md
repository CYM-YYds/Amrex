# B 冻结优化版本从头重跑（622137）

正式运行，保留旧job621436/B不修改。复用scripts/temporary/a5_threshold_probe.sh及其环境参数。snapshot含提交时正式源/配置/可执行、历史输入和设备采样rank快照。

参数：FLOAT、D3Q27、Re1000、base128³、lev2、max_grid_size128/256/512、BF16、grid_eff0.7、err=.125/.25/.5/1、regrid32，max_step128000、stop_time1000，从静止启动；plot12800、checkpoint32000且保留各检查点、report3200；收敛检测开启但不停算，保留regrid_valid_check和final_state_check。正式用户inputs不改。

统计：以512³均匀网格为最细分辨率参照。每次网格快照分别计算分配valid总数ΣN_l、去覆盖叶子单元Σ(N_l−N_{l+1}/8)，相对512³给出存储网格比与节省比例；halo与临时数组单独影响实际显存，不能由网格比替代显存比。AMR有效更新量额外含2^l子步权重。完整累计perf.total、solv、分项及整次Total Time分别记录，不混为同一分母。

显存：整次The Arena使用高水位与保留高水位分列；GPU总用量每秒采样记录UUID及其他基线进程，采样峰值可能漏过短暂峰值。默认Arena设置沿用旧B，不将30GB预留当作10GB有效使用。均匀512³已有622016短测：Arena使用34094 MB、保留34102 MB、关闭输出，约4058 MLUPS；节点/时间范围/输出及Arena初始预留不同，当前仅为参照，不能宣传同条件长期性能或全设备峰值节省率。

保留源、程序、输入、命令、设备采样、日志、场输出和检查点。运行完成后由原始快照汇总最大/最终网格压缩率、显存节省比例及完整性能。

2026-11-02 核实：job622137/B 已正常完成 128000 步、t=1000，调度状态 SUCCEEDED、exit_status=0，终点场和检查点保留；converged=0，不宣称收敛。上述 regrid_valid_check 属于本次冻结的历史配置，现役源码已退役该开关。本长跑早于后续重构检查剥离、缓存复用和分项计时变更，不能作为当前 HEAD 的长程数值验收。现役成本结论见[重构报告](../../docs/ai_changes/2026-11-02-regrid-cost-quantification.md)。
