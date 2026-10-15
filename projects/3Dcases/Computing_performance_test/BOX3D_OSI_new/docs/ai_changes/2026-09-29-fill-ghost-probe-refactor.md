# FillGhostLevel 诊断结构重构

- 目的：将 `FillGhostLevel()` 中首次 ghost 插值数据导出的诊断逻辑与生产 ghost 填充流程分离。
- 文件：`src/AmrCoreLBM.cpp`、`src/AmrCoreLBM.H`。
- 主要改动：新增 `ProbeFillGhostLevel(int lev)`，集中处理 probe 文件输出；`FillGhostLevel()` 只保留 OSI/A-B ghost 填充分派、probe 开关和首次调用控制。
- 验证：`git diff --check` 通过；目标 CUDA + MPI 算例编译成功，生成 `main3d.gnu.TPROF.MPI.CUDA.ex`。
