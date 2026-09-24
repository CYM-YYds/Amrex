# BOX3D_OSI 重网格后 valid 状态检查

- 目的：确认新增或重建 AMR 层的全部 valid DDF 是否有限，并区分有限性与 A-B/OSI 一致性。
- 修改文件：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/AmrCoreLBM.cpp`。
- 主要改动：`verification.regrid_valid_check=true` 时，`RefineMesh()` 返回前按当前 phase 解码每层全部 valid DDF，逐分量检查 NaN/Inf。启用 `verification.osi_ab_check` 且 level 0 存在 A-B 参考时，额外输出全 valid 最大差值；新增细层没有同步构建 A-B 参考，日志明确标为 `ab_reference=0`。
- 验证：GPU 编译成功。作业 603276 在第 32 步重网格后，level 0/1 全部 valid DDF 均有限；level 0 全 valid A-B/OSI 最大差为 0.07579002442。作业 603277 运行至第 96 步并正常结束；第 32、64、96 步重网格后所有存在的 level 0/1/2 valid DDF 均有限。该检查不证明新增细层与独立 A-B 运行数值一致，也不检查 ghost。
