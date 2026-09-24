# BOX3D_OSI 诊断交接文档对齐

- 目的：把 2026-09-24 的源码、工作配置和运行诊断对齐到唯一现役交接入口，防止把历史性能和旧 fallback 说明误作当前结论。
- 修改文件：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/README.md` 及该算例 `docs/current_status.md`、`docs/MLUPS记录.md`、`docs/osi_algorithm_and_architecture.md`、`docs/osi_mpi_communication_plan.md`、`docs/osi_parallelcopy_optimization_plan.md`；本记录。
- 主要改动：记录当前未提交实验输入；说明通信现走 raw 路径、平均 restriction 采用局部 canonical 工作区；将旧计划和 596890/596891 性能数字标为历史；记录 603283 的第 31 步阶段零差与第 32 步 repair 前全 valid 非零差之矛盾，以及下次逐点测量位置。保留现有运行产物和用户源码/配置修改。
- 验证：核对当前 `CommunicateOsiLevel()`、`AverageDownOsiValidLevel()`、`config/inputs` 与 jobs `603273`、`603276`、`603277`、`603283` 日志；文档范围 `git diff --check` 通过。此次仅改文档，没有重新编译或提交计算作业；数值根因仍待验证。
