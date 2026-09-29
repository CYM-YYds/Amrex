# AverageDownOsiLevel 分支结构整理

- 目的：避免单 rank direct 写回路径看起来受 `osi_sync_batch_components` 控制。
- 文件：`src/AmrCoreLBM.cpp`。
- 主要改动：单 rank 且 `osi_parallel_copy=1` 时单独走全部 Q 分量 direct kernel；其余路径单独使用 `osi_sync_batch_components` 分批 canonical staging 和 OSI raw 写回。
- 验证：目标 CUDA + MPI 算例编译成功，生成 `main3d.gnu.TPROF.MPI.CUDA.ex`。
