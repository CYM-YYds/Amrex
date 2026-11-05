# 最新优化C组完整重跑（2026-11-05）

本次任务复现入口，随正式结果保留。替代旧624717/C及625774+625855/C的程序版本；历史结果不覆盖。新编排负责冻结副本构建、短回归和长跑串联；实际求解、监护、本地落盘和SHA回传复用`a5_threshold_probe.sh`与`checkpoint_io/paired_paths.py`，避免复制已有I/O实现。

冻结材料位于`runs/C_optimized_t1000_20261105/`：提交前当前工作树src/config、git_head、未提交源码差异、inputs，另有仅在副本补齐A-B128线程的experiment.diff。正式源码不修改、不提交用户其他任务的修改。

- 单A100/单rank，FLOAT/D3Q27、BGK、Re1000、C阈值0.25/0.5/1/2、128³基础网格、max_level2、BF16、逐层max_grid_size128/256/512，regrid每32粗步。cavity、IBM关闭、参考速度0.05、参考长度1。
- 从零到128000粗步/t1000；每3200步保留速度、Q/涡量，各40帧；每32000步保留checkpoint，共4份。开启收敛监测但不提前停止。
- OSI启用现役批量插值/平均/边界、128线程及坐标复用；A-B使用相同批量机制并在副本补齐128线程。通信隐藏单rank回退，碰撞批量不采用。
- 两组按OSI→A-B同节点串行，各自按32000步分段，32000/64000/96000恢复，恢复次数一致。每段本地写出、共享盘普通内存分块回传、fsync和逐文件SHA回读，成功后清理节点临时副本；失败保留现场。节点实际可用约16.7GiB，依据历史C组96k段输出约17GB，32k分段设置12GiB容量门槛，原通用入口默认仍为20GiB。
- 先以同程序从零运行128步，双方保存64/128步输出，完整valid（含covered）逐值检查完全一致且有限才启动生产。该检查不是长期物理等价证明。

在BOX3D_OSI目录提交：

```bash
dsub -s scripts/temporary/C_optimized_t1000/submit.sh
```

`run.py`使用已准备的冻结build，作业内`make -j8 DIM=3 PRECISION=FLOAT`构建并记录SHA。源码准备时复制当前src/config/GNUmakefile及已有构建对象，所有源码更新时间以强制重编译；A-B两个寄存器碰撞调用替换为ParallelFor<128>，差异保存在experiment.diff。保留构建对象直到任务完成。

查看`runs/C_optimized_t1000_20261105/job<id>/`的`gate_passed.txt`、`progress.json`、`failure.json`及`completed.txt`。生产每段独立保存原始日志、命令和transfer_manifest；`production/C_mode1`、`production/C_mode0`为连续流场序列入口和汇总日志。

完成后`summary.json`、`report.md`自动核验两模式40个性能窗口、40+40帧和4份checkpoint。累计total含重网格，排除恢复/输出/回传；历史表使用末窗口MLUPS，另列全程累计更新量/累计total。不能把程序总时长或不同网格轨迹的时间比当作严格同工作量算法贡献。
