# BOX3D_OSI 稀疏 interface 平均写回范围修正

- 目的：避免 OSI 平均回退路径从未填充的 staging 单元读取并改写粗层 interface。
- 文件：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/AmrCoreLBM.cpp`；同步该算例的 `docs/current_status.md`。
- 改动：回退路径的 `ParallelCopy` 后，只在稀疏 `interface_result` 与本地粗网格 valid 的交集启动编码写回，保留 `interface_mask` 检查。
- 验证：CUDA+MPI 构建成功；单 GPU 作业 `603393` 在第 32 步的平均入口与旧回退作业 `603385` 逐字节相同，平均出口与 direct 作业 `603386` 逐字节相同。回退写回数量由 39,880 降为 16,384 个粗单元 × 27 分量；uncovered 与其余 covered 未改写。平均后 interface 对 A-B 的最大差仍为 `0.004901078005466977`，多 rank 路径待测。
