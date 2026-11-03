# C 阈值参数与性能入表

目的：将已完成C/624717的正式统计填入MLUPS记录的参数表、性能表。仅BOX3D_OSI文档和结果汇总，不改求解器或运行参数。保留用户已有标题/排版修改，仅暂存本次新增内容。

核对冻结inputs/overrides/manifest、FLOAT和A100记录、40个连续3200步perf窗口、退出0及step128000/t1000检查点。各阶段及solv/total用Decimal累加，MLUPS取最后一个3200步窗口，与表中B口径一致；独立机器可读汇总保留于runs/a5_C_t1000_gif/job624717/C_performance_table_summary.json。

累计interp317.81、average140.41、collide2345.67、stream8.25、comm105.40、boundary416.65、solv3333.35、total3483.09秒；末窗MLUPS_solv2001.30、MLUPS_total1908.63。整次程序Total Time3635.30秒和guard3637.95秒独立注明。Arena active/allocated高水位5049/30254MiB，Pinned active864MiB，未采集设备峰值，不补造。终态converged=0。

C使用历史冻结程序，与表中B优化程序的版本和输出频率不同（两作业分配记录均为agent-82，但运行时刻不同），文档明确不将两者时间或显存差异仅归因于阈值。没有使用失败AB作业或128步短测代替完整C结果。验证：40窗逐一对齐、数值舍入核对、来源相对链接与路径存在、git diff --check；无构建或数值路径修改，不新增求解器运行。
