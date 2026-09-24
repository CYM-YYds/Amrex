# BOX3D_OSI 第 64 步独立 OSI 与锁步 OSI 对比

- 目的：检验 `verification.osi_ab_check` 是否改变 OSI 本身的数值结果，并使锁步参考态在重网格前经历完整平均与每步界面平均。
- 修改文件：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/AmrCoreLBM.cpp`、`projects/3Dcases/Computing_performance_test/BOX3D_OSI/docs/osi_ab_stage_evidence_2026-09-24.md`。
- 主要改动：锁步模式对 A-B 参考态补做 `AverageDownValidLevel` 和 `AverageDownInterfaceLevel`；新增只用于诊断的 `verification.osi_ab_continue_on_mismatch`，保留报警并允许完成 checkpoint，且完成提示不再声称检查通过。独立 OSI 路径未改变。
- 验证：`GEN_CCDB=0 ./scripts/compile.sh --no-submit` 构建成功。相同可执行文件的独立 OSI 与锁步 OSI 在 step 63 的 checkpoint 逐文件一致；step 64 的 level 0/1 valid DDF 不同，level 2 一致，均无非有限值。level 0/1 uncovered 最大差分别为 `0.01317620816586777`、`0.019716771881433615`。锁步参考态在 step 33–63 的阶段检查通过，step 64 重网格前的完整平均/边界修复后报警。作业、命令及逐值统计见算例的阶段证据文档。
- 限制：checkpoint 不保存 ghost；第 64 步的 OSI 分叉尚未区分为完整平均、边界修复、重网格构造或后续推进中的具体操作。`osi_ab_continue_on_mismatch` 开启时，参考态已经失配，后续参考态报警只能用于定位，不能视为数值正确性结论。
