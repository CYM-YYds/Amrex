# OSI 通信骨架继续封装

- 目的：继续减少插值、平均和重构路径中重复的 CPC MPI 搬运代码。
- 文件：`src/AmrCoreLBM.cpp`、`src/OsiCommunication.H`。
- 主要改动：插值和界面平均复用统一的 `parallel_copy_mpi()`；本地交集复用 `parallel_copy_local()`；统一 cell 到聚合 buffer 的索引计算；保留各阶段原有 phase 映射、host/device transport 和计时项。
- 验证：CUDA + MPI 编译通过（`compile-20260929T222911-summary.log`）；尚未完成新的单/多 rank运行对照。
