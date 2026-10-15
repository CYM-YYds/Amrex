# OSI phase-aware 重构迁移

- 目的：在 OSI regrid 时避免使用 `osi_sync_buffer` 作为旧布局迁移的 canonical 中间缓冲；布局未变化时保留旧 raw state 和 phase。
- 文件：`src/AmrCoreLBM.cpp`、`src/AmrCoreLBM.H`。
- 主要改动：新增 `RemapOsiLevelDirect()`，复用 AMReX CPC 生成旧/新 Fab 的本地和 MPI 区域，旧 phase raw 数据直接打包/写入新 phase=0 raw 状态；新增布局相同快速路径。
- 验证：目标 CUDA + MPI 算例编译成功，生成 `main3d.gnu.TPROF.MPI.CUDA.ex`。尚未完成多 rank regrid 数值对照。
