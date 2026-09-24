# BOX3D_OSI 时间步平均覆盖全部 interface 标记单元

- 目的：按用户确定的区域语义，让普通时间步的 A-B 和 OSI 平均覆盖全部 `interface_mask` valid 单元，包括被细层覆盖且贴非周期物理边界的单元。
- 文件：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/AmrCoreLBM.cpp`；同步该算例的 `docs/current_status.md`。
- 主要改动：`BuildAverageCache()` 在既有域内粗细交界候选之外加入非周期物理边界面，去重后构造两种模式共用的稀疏限制缓存；断言缓存数等于 `interface_mask` 标记数。OSI 回退继续只从有实际限制结果的 staging 位置写回。
- 验证：CUDA+MPI 编译成功。单 GPU 第 32 步作业 `603408`、`603409`、`603410` 的 A-B、OSI 回退、OSI direct 均改写全部 39,880 个 interface 粗单元 × 27 分量，且不改写其他 valid；两条 OSI 出口相同，全部有限。第 33 步 `603411`/`603412` 的已知 uncovered 采样差异仍存在。多 rank 和更长程数值稳定性尚未验收。
