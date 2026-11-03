# 真实A-B短程VisMF跟踪与文件完整性检查

目的：承接独立write探针成功，用户要求下一步真实AMReX输出定位；仅BOX3D_OSI诊断，不改数值路径或用户配置。

扩展既有a5_threshold_probe.sh增加可选PROBE_TRACE_IO，只在复制出的rank包装器应用调用前插入strace，原MPI/rank退出留证逻辑不变，默认关闭。新增checkpoint_io/analyze_vismf_trace.py负责从系统调用和FAB文件验证输出完整性，既有double固定尺寸分析器不能复用当前FLOAT多Fab输出。

# 真实 AMReX VisMF 输出系统调用测试

| 作业 | 节点 | 运行范围 | 输出结果 |
|---|---|---|---|
| 624889 | agent-193 | 从C step32000检查点，以A-B推进到32004，每步写出 | 4份速度+4份Q涡量，成功 |
| 624890 | agent-67 | 从静止A-B推进3200步，首次输出 | 1份速度+1份Q涡量，成功 |

两作业SUCCEEDED，guard和rank退出0；共300条场数据写系统调用，未发现PlotFile路径相关的失败系统调用。全量回读10份PlotFile的FAB，文件长度/完整性及全部427704320个浮点值有限性检查通过（只验证输出完整性，不代表数值准确或两模式等价）。记录见trace_validation.json。原始strace保留write/writev/pwrite64/fsync/fdatasync/openat/close的路径、errno与耗时。

从静止测试对准原先step3200首次输出，但为诊断关闭了收敛监测和checkpoint，非完整长程配置重复；检查点组改变了起点，均不作为性能结果。两组使用原C冻结程序、阈值0.25/0.5和stream_mode0，未修改求解器。

当前结论：真实AMReX短程输出也能成功，本轮未复现之前的VisMF故障。不能据此确定根因在超算，也不能证明长程问题已修复；strace可能影响故障时序。原失败节点未在本轮覆盖，后续定位应保留同类跟踪并核对失败errno，不直接把短测成功推广为长期可靠性。

保留全部输入、冻结程序/源码、命令、日志、跟踪和10份完整场文件。分析命令：仓库根 .venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/checkpoint_io/analyze_vismf_trace.py projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/C_AB_VisMF_trace。

验证：bash -n、Python语法、实际两组GPU短程、退出码、300条数据写入调用、所有FAB全量有限值/长度检查、git diff --check通过。不重新编译冻结历史程序，不宣称长期精度或故障修复。
