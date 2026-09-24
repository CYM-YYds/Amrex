# BOX3D_OSI 首次插值返回点核对

- 目的：在第 32 步 `FillGhostLevel` 返回后、任何层推进前核对所有现存层的 uncovered valid。
- 修改文件：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/main.cpp`；该算例的 `docs/current_status.md`。
- 主要改动：增加默认关闭的 `verification.check_after_first_fill`；启用 OSI 锁步参考态时，在 `Cycle2(0)` 首次插值返回处调用现有逐层诊断。
- 验证：GPU/MPI 编译成功；job `603455` 在该测点测得 level 0 主机逐值零差、level 1 参考态有限且阶段范数零差。随后在 level 1 首个 `Stream` 后按预期于首次差异中止。level 1 插值返回点仍缺独立主机逐值核对。
