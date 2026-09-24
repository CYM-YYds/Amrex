# BOX3D_OSI uncovered A-B/OSI 锁步诊断

- 目的：定位当前 OSI 与 A-B 在求解器拥有的 uncovered valid 单元上的第一处差异。
- 修改文件：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/AmrCoreLBM.cpp`。
- 主要改动：`verification.osi_ab_check` 的阶段比较跳过 covered valid 单元；通信阶段同样只比较 valid；空比较区域的范数记为 0。
- 验证：诊断版编译成功；单 GPU、Re=1000、三层 AMR、`osi_parallel_copy=0`、`osi_mpi_direct=0` 的作业 603273 在第 33 步 level 0 Stream 阶段首次报告 uncovered 差异。坐标 `(1,111,111)`、q=18，A-B 为 0.018745472003403797，OSI 为 0.018585093245395951，差值 1.6037875800784668e-4。第 32 步发生重网格；此前比较阶段没有报告 uncovered 差异。该结果只定位第一处输出差异，不判定上游数据的根因。
