# 新层 OSI direct 复制去除分批循环

- 目的：淘汰 OSI direct 生产路径中按 `osi_sync_batch_components` 拆分粗层 DDF 的循环。
- 主要改动：新细层初始化时用 `ParallelCopyOsi` 一次复制完整 Q 分量，再执行一次 canonical 缩放和一次粗到细插值；A-B 与 direct 关闭路径继续保留原分批回退实现。
- 说明：`ParallelCopyOsi` 只负责 phase-aware raw 数据搬运，不能替代缩放或插值，因此三阶段仍保持独立。
- 验证：需重新执行 CUDA+MPI 编译，并用两 GPU 多层冒烟脚本验证新层初始化、动态 regrid 和 A-B 有效单元逐点结果。
