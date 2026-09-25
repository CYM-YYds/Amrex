# BOX3D_OSI：定位第 64 步 uncovered 首次差异

## 修改目的

将第 64 步重网格前的 `AverageDownValid()` 与
`RepairCurrentStatePhysicalBoundary()` 分开检查，只统计 A-B 参考态与 OSI
状态的 uncovered 区域差异。

## 修改文件

- `projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/main.cpp`

## 主要改动

新增诊断覆盖项 `verification.osi_check_after_average_valid`。启用后，在
`AverageDownValid()` 返回、物理边界修复之前调用
`CheckOsiReferenceLevel0(step, "AfterAverageDownValid")`；默认关闭，不改变生产
推进路径。

## 验证结果

- 编译：`logs/compile/compile-20260925T132546-summary.log`，成功。
- 作业：`runs/20260925_132837_job603718/`，第 64 步、正常重网格、锁步诊断。
- `StepEntry`：`uncovered_unequal=0`。
- `AfterAverageDownValid`：`uncovered_unequal=49,545,216`，首次出现 uncovered
  差异。
- `AfterRepair` 与 `AfterRefineMesh`：差异仍在，没有新增首差。

本次只增加定位测点，没有修改平均、边界或重网格算法。
