# C 从静止到 t=1000，供 GIF 使用（正式实验、长期保留）

沿用 C 短测相同冻结 FLOAT 程序和原始输入，C 阈值0.25/0.5（完整数组0.25/0.5/1/2），Re1000，128³，最高level2，BF16，grid_eff0.7，regrid_int32。
从静止开始，checkpoint.begin_step=0，无重启；终点128000步、物理时间1000。每3200粗步即25物理时间输出一次：预期40份速度plt_*与40份Q/Vorticity plt_vort_*，覆盖t=25..1000。现有程序不额外输出t=0帧。
每32000步保存checkpoint，keep_latest_only=false。连续3次低于1e-4判定正式收敛但不提前停止；正式监测3200步、趋势1000步。
保留全部场数据、checkpoint、输入、程序/源码、指纹、命令、日志及报告，供后续ParaView动画/GIF使用；不自动清理。提交不表示完成或收敛。
复用现有a5_threshold_probe.sh，只新增可选PROBE_RANK_SCRIPT以复用快照中语义相同的B rank包装器，目标输出目录仍为C。当前用户config/inputs不用于此次运行。
