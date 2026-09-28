# BOX3D_OSI 全层级边界与 Stream 源区域诊断

- 目的：区分物理边界处理后的 uncovered 差异，以及 Stream 读取的 interface、valid 和 ghost 源区域差异。
- 修改文件：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/AmrCoreLBM.H`、`src/AmrCoreLBM.cpp`、`scripts/submit_osi_lockstep_alllevels_64.sh`。
- 主要改动：锁步模式在每个 AMR 层级增加 `BoundaryUncovered` 检查；在通信后按每个 uncovered Stream 目标和 q 比较实际 pull source，并分别统计 valid、covered/interface、物理边界 ghost 和内部 patch ghost。
- 验证：远程 CUDA+MPI 编译成功；作业 604902（64^3、max_level=2、max_step=64）成功。第 64 步 level 0 和 level 1 首发目标均标记为 `physical_boundary=0`，边界后差异保持不变。level 0 首发源包含 interface/covered 差异，level 1 首发源包含内部 patch ghost 差异；level 2 uncovered valid 仍一致，但后续 source 检查发现物理边界 ghost 差异。
