# 独立检查C A-B失败后的共享存储写入

目的：用户要求先测正常写入，暂停完整长跑；范围BOX3D_OSI既有checkpoint_io独立探针，不改求解器、数值配置或已有结果。

# C A-B 写入故障：独立写入测试

| 作业 | 节点 | 写法 | 每次数据量 | 普通内存 | CUDA锁页内存 | 刷新/全字节回读 |
|---|---|---|---:|---:|---:|---|
| 624881 | agent-187 | write | 216 MiB | 3/3成功 | 3/3成功 | 全通过 |
| 624882 | agent-67 | writev | 81B头 + 1536 MiB | 3/3成功 | 3/3成功 | 全通过 |

共12次写入，数据载荷约10.27GiB，write/writev返回完整长度、errno0，fsync0，文件长度与逐字节回读全部符合预期；探针编译成功、退出0，两作业SUCCEEDED。文件写入与失败算例相同的共享挂载，系统调用记录syscalls.log、挂载记录mount.txt及最终heap.dat/pinned.dat均保留。

本次证明这些节点当前可以完成上述大块写入；不复现原AMReX多Fab/动态AMR输出流程、不覆盖原失败节点，未重现VisMF故障，不证明长期可靠性或故障根因已修复。之前失败结果仍保留。下一步如继续定位，应在真实AMReX首次输出处跟踪write/writev等系统调用以获得errno，而不是直接重跑完整长算例。

复用scripts/temporary/checkpoint_io已有探针；新增重复次数参数、全字节回读校验和失败非零退出，提交入口只新增IO_OUTPUT_ROOT。输入/源/编译日志/命令/系统调用/结果和文件保留为科学诊断材料。

验证：bash -n、GPU节点nvcc编译与12次实际写入/fsync/全字节回读通过；syscalls退出0；git diff --check。详细结果runs/temporary/C_AB_write_probe/report.json。
