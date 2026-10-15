# 单 rank OSI/A-B 256 步 uncovered 对照

- 配置：job `606057`，单 rank/单 GPU，64^3，`max_level=2`，`regrid_int=32`，`max_step=256`，`verification.osi_step_entry_check_interval=1`。
- 结果：step 1--256 正常结束；入口 uncovered 逐值检查为 level 0 `256` 次、level 1 `224` 次、level 2 `192` 次，全部 `linf=0`；推进阶段 `BoundaryUncovered` 也全部 `linf=0`。
- 证据：`runs/20260930_083408_job606057/run.log`，末尾包含 `Total Time: 1118.013971` 和 `AMReX (26.06) finalized`；未发现 `osi_ab_stage_failure`、`finite=0` 或 `failure_kind`。
- 边界：`osi_ab_stream_sources` 的 ghost source 诊断仍可能报告差异；它不是 uncovered valid DDF 对照。本作业工作树为 dirty，运行使用 executable SHA256 `b2c4c9cbc377725f91671bc8cb1124fb2ac554cb677f36ae71c0d68711d4e9ac`。
