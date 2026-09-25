# BOX3D_OSI 第 64 步阶段指纹诊断

- 目的：在重网格、插值、`AdvanceLevel` 和 `AverageDownInterfaceLevel` 的调用边界，直接比较独立 OSI 与锁步诊断中的 OSI 状态。
- 修改文件：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/main.cpp`、`src/AmrCoreLBM.H`、`src/AmrCoreLBM.cpp`，以及阶段证据文档。
- 主要改动：增加只由 `verification.osi_stage_trace_step` 启用的 grown-Fab 位模式指纹，分别报告 uncovered valid、covered valid 和 ghost；在 `Cycle2` 和第64步重网格路径插入阶段采样。
- 验证：CUDA 编译成功。独立 OSI job 603658 与锁步 OSI job 603659 在第64步共 35 个阶段记录全部一致，且 level 0/1/2 checkpoint 的 Header 与 DDF 文件逐字节一致。阶段记录见 `runs/20260925_084420_job603659/osi_stage_trace_comparison.txt`。
- 限制：指纹用于阶段筛查，不替代逐点差值；当前复测没有复现此前 603512/603513 的 checkpoint 差异，因此不能据此断言历史差异的根因已经消失。
