# BOX3D_OSI：分离生产模式与锁步检测入口

## 修改目的

让 A-B 生产、OSI 生产和 OSI/A-B 锁步检测成为明确的运行模式，避免
`main.cpp` 在生产循环中直接解析和编排检测参数。

## 修改文件

- `projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/AmrCoreLBM.H`
- `projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/AmrCoreLBM.cpp`
- `projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/main.cpp`
- `projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/OsiAbVerification.H`
- `projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/OsiAbVerification.cpp`
- `projects/3Dcases/Computing_performance_test/BOX3D_OSI/config/Make.package`

## 主要改动

- 根据现有输入兼容地解析为 `CanonicalAB`、`OsiProduction` 或 `OsiLockstep`。
- 抽出 `AdvanceOneLayout()`；生产模式只推进一次布局，锁步模式显式推进 OSI 和
  canonical 两种布局。
- 新增 `OsiAbVerification`，集中管理重网格前、平均后、Boundary 后、重构后、首次
  ghost 填充和时间步后的检测调用。生产模式不调用这些入口。
- 生产 OSI 仍不分配 A-B 两个 canonical 数组；锁步模式继续分配并保持原有阶段比较。

## 验证结果

- 编译：`logs/compile/compile-20260925T211908-summary.log`，成功。
- OSI 生产 64 步：`runs/20260925_205905_job603769/`，日志中的
  `ab_check=0`、`full_ddf_arrays=1`，正常完成。
- A-B 生产 64 步：`runs/20260925_205942_job603770/`，正常完成。
- OSI 锁步 64 步：`runs/20260925_211454_job603772/`，`run_mode=OSI-lockstep`、
  `full_ddf_arrays=3`，`AfterAverageDownValid`、`AfterRepair` 和
  `AfterRefineMesh` 检测均正常输出，uncovered 差异保持为 `0`。

本次先拆分运行模式和检测编排；A-B reference 数据仍由 `AmrCoreLBM` 在锁步模式
拥有，以保持现有数值路径不变。后续如需进一步减少类内检测代码，可再迁移比较实现
本身，不与本次模式拆分同时改变数值算法。
