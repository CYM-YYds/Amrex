# AMR输出/检查点写入故障排查

日期2026-11-01。正式源码、AMReX和用户inputs均未修改。失败样本622026在速度场写入、622111在checkpoint首层写入失败，文件仅81/82字节FAB头；错误发生在VisMF检查文件流状态的环节，旧日志未记录底层errno，因此不能追溯判定EIO、EFAULT、配额或空间问题。

## 当前源码核对

WriteCheckpoint先DecodeOsiValid到canonical GPU MultiFab，再调用VisMF::Write；AMReX把GPU数组搬到The_Pinned_Arena，同步后从主机缓冲写文件。checkpoint曾在789bb2c将重复解码改为共享DecodeOsiValid，并非从未改动；近期碰撞128线程/坐标复用未改VisMF文件后端。此次没有依据去改碰撞或checkpoint数值公式。

失败节点82挂载类型实查为dpc（共享存储），不是此前猜测的NFS；df空闲与quota服务不可查询都不足以证明失败原因。不能把写入失败叫作OOM或数值发散。

## 同一失败节点的诊断

| 作业 | 对照/范围 | 结果 |
|---|---|---|
| 622130 | 不运行LBM：普通heap与cudaHostAllocMapped内存，各三次216MiB write+fsync，strace捕获错误 | 六次完整写入、errno0、fsync成功 |
| 622132 | 当前正式程序从旧B有效step32000/t250 checkpoint重启128步，输出场及三层checkpoint；跟踪失败write/writev/pwrite64/fsync/openat | 退出0，约2.9GiB checkpoint写出成功 |
| 622134 | 同源同节点，从同一输入连续8轮（每128步）场及checkpoint输出 | 退出0，8个checkpoint成功，无EIO/EFAULT/ENOSPC/EDQUOT |
| 622135 | 读回622132产生的checkpoint，不推进，再与该文件逐值比较 | 退出0，三层27分量valid/active范数0 |

读回对照验证的是文件可读取及重启编码的一致性，不是独立的写出前内存对照或长期精度证据。密集输出与独立内存写入均未复现故障，不能据此宣布根因已解决；可以排除这些已测条件下的稳定必现故障。

## 有诊断证据的完整尝试

622137从静止按原B全部覆盖参数到128000步/t1000，保留场输出和checkpoint，当前提交/运行状态待终态检查。沿用当前冻结程序，rank快照加strace仅保留失败系统调用，日志io_trace.*记录fd路径、errno，供下次失败定位。跟踪仍有运行开销，其耗时不能不加说明替代无跟踪性能基线。本轮不再修改数值实现，也不关闭输出规避问题。此诊断rank未额外加入设备采样；精确Arena使用高水位仍由退出日志给出。

保留实验目录全部输入、命令、源、程序、trace、checkpoint及结果；未清理诊断文件。原失败目录不动。未取得errno前，存储故障和长程运行环境因素仍待证；下一步首先核对622137终态及失败系统调用，而不是重复无诊断长跑。

[核对汇总](checks.json) · [正式重跑说明](../../a5_B_optimized_128000/README.md)
