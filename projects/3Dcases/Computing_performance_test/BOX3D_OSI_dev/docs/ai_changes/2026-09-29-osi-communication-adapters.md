# OSI 通信适配接口

- 目的：让同层 ghost 同步和重构时的旧/新布局迁移各有一个明确的 OSI 入口，并集中重复的本地 kernel 与 MPI 传输骨架。
- 文件：`src/AmrCoreLBM.H`、`src/AmrCoreLBM.cpp`、`src/OsiCommunication.H`、`docs/current_status.md`、`docs/osi_algorithm_and_architecture.md`。
- 主要改动：增加 `FillBoundaryOsi()` 统一选择同层 direct/fallback；增加 `ParallelCopyOsi()` 接受源/目标 phase 并处理 CPC 本地和跨 rank 迁移；抽出 `parallel_copy_local()`、`parallel_copy_mpi()`，保留 regrid 原有通信顺序与 host/device staging 选择。插值和界面平均的混合布局传输仍由其专用路径处理。
- 验证：CUDA + MPI 构建通过；动态重构的单/多 rank 逐值验收仍待完成。本次编译工作树含有用户预存的平均界面 mask 修改，因此编译证据不单独证明该修改的数值行为。
