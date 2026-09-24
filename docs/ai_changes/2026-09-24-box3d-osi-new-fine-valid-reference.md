# BOX3D_OSI 首次重构后细层 valid 对照

- 目的：确认第 32 步首次重构后，新增 level 1 的全部 valid DDF 是否与 A-B 参考态一致。
- 修改文件：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/AmrCoreLBM.cpp`。
- 主要改动：仅在 `verification.osi_ab_check=true` 时，新增细层同时从粗层 canonical 参考态构造 A-B 插值结果并分配锁步推进缓冲区；重构后逐层检查参考态有限性和 OSI/A-B valid 差值。
- 验证：CUDA/MPI 编译通过。job 603355 在第 32 步 `AfterRefineMesh` 得到 level 0 全 valid 主机逐值差异为 0、level 1 全 valid 比较范数为 0；随后 level 1 `Stream` 首次报差，最大 `3.8184455009127732e-05`，诊断按预期中止。job 603351 是补齐细层参考推进缓冲区前的失败尝试，不能作为数值结论。
