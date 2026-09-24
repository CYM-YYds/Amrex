# BOX3D_OSI 第 32 步边界修复前后测量

- 目的：区分 level 0 全 valid A-B/OSI 差异出现于边界修复、重网格，还是更早阶段。
- 修改文件：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/AmrCoreLBM.cpp`。
- 主要改动：启用 `verification.osi_ab_check` 时，在 `RepairCurrentStatePhysicalBoundary()` 前后按当前 OSI phase 解码 level 0 全 valid DDF，并用同一分量批次与 A-B 参考计算最大差及非有限状态。
- 验证：GPU 编译成功。作业 603283 在第 32 步输出修复前、修复后、`RefineMesh()` 后的全 valid 最大差，三者均为 0.07579002442；全部 valid DDF 有限。该差异在边界修复前已存在，不能归因于第 32 步边界修复或新增 level 1。第 31 步阶段比较输出为 0，与这项全 valid 检查不一致，需进一步核对两种诊断的覆盖范围与比较实现。

> 2026-09-24 更正：上述 `0.07579002442` 是旧批次范数诊断的错误输出，
> 不能解释为真实 DDF 差异。修正速度表后的主机逐 cell、逐 q 检查（jobs
> `603306`/`603307`）确认 step 32 入口、修复后、重网格后的 level 0
> 全 valid 全部相等。现役结论见算例 `docs/current_status.md`。
