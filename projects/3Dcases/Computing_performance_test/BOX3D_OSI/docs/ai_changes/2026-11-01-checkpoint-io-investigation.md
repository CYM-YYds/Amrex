# checkpoint/PlotFile写入故障分层诊断

目的：区分近期数值优化、GPU到主机准备、内存种类与共享存储写入失败。仅新增独立临时216MiB普通/锁页内存对照（现有工具不能隔离缓冲类型），复用a5_threshold_probe.sh进行真实三层短程输出；不改求解器、AMReX或生产输入。

同失败节点82：622130六次write+fsync成功；622132真实三层约2.9GiB检查点成功；622134八轮场/检查点成功；622135读回三层27分量范数0。strace均未复现EIO/EFAULT/ENOSPC/EDQUOT。实测文件系统类型dpc，修正此前NFS猜测。根因仍未确认，不称为已修复。检查点解码曾在789bb2c复用DecodeOsiValid，近期碰撞优化未改VisMF写入。

622137保持原B参数从静止跑完整t1000，带失败写系统调用跟踪以取得errno；日志未跟踪成功调用，但ptrace仍有开销，性能另标。未以短测通过冒充全程完成。

验证：独立CUDA探针编译/运行通过，Shell语法、相关链接和本次diff检查通过；保留用户MLUPS记录修改及六个历史日志删除，不纳入提交。[证据、边界与后续](../../runs/temporary/checkpoint_io/report.md)。
