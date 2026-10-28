# main 生命周期函数拆分

## 目的与范围

整理 BOX3D_OSI 的程序入口，让初始化、重构、性能报告和输出的职责明确。
只改 `src/main.cpp`；工作区原有 `AmrCoreLBM_advance.cpp`、`Kernels.H` 修改及其他文件删除不属于本次提交。

## 主要改动

- 在 main.cpp 匿名命名空间新增 RunSimulation、InitializeSimulation、RegridIfNeeded、ReportPerformanceIfNeeded、WriteOutputsIfNeeded。
- 复用现有 ReadInputConfig、Cycle2 和 AmrCoreLBM 的操作；这些是程序级生命周期编排，无需向求解器增加接口或创建新模块。
- PerformanceWindow 只拥有本次 RunSimulation 的窗口累计量，每步累加，报告后与求解器统计同步清零；运行参数继续以 lid.params() 为唯一来源。
- 保持重构前完整平均、验证钩子、时间推进、收敛检查、性能报告、PlotFile 和 checkpoint 的顺序，保留计时范围、日志字段及 AMReX Finalize 前析构的要求。
- 输入解析和所有 Cycle 推进函数内容不变，保留原文件换行格式。

## 验证

- 使用既有 compile.sh，三维 DOUBLE CUDA 编译通过。编译器仍报告 LbmLattice.H 的已有 always_inline 属性警告。
- 作业 621219：原版/整理版各运行 8 步，另做第 4 步 checkpoint 重启和单层锁步参考钩子检查。
- 作业 621221：原版/整理版各运行 40 步，第 32 步受控创建 level 1，并从第 20 步 checkpoint 重启至第 40 步。
- 7 组运行全部完成，FinalState 活跃单元有限值检查通过；整理版与原版、重启与连续运行的逐层 valid/active DDF 差异范数均为 0。
- PlotFile/checkpoint Header 检查通过；性能工作计数前后一致，单层验证入口和推进钩子未报 mismatch。
- 输入解析和所有 Cycle 函数与拆分前逐字一致（换行编码归一化比较）；Shell/Python 语法检查及本次 diff 检查通过。
- 首次作业 621218 因新脚本缺执行权限在应用启动前退出，修正后重交，失败日志保留。

边界：仅单 GPU、小网格短测，未覆盖二维、FLOAT、多 rank、粒子或长期物理精度；本次不评价性能收益。

实验目录：`runs/temporary/main_lifecycle_refactor/`，为临时短测；保存输入、程序、源码快照、已有修改快照、构建与运行日志和关键输出。
复现入口：`scripts/temporary/main_lifecycle_refactor_test.sh`，作为本次结果的复现材料保留。
