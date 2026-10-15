# 新层 OSI direct 复制去除分批循环

- 目的：淘汰 OSI direct 生产路径中按 `osi_sync_batch_components` 拆分粗层 DDF 的循环。
- 主要改动：新细层初始化时用 `ParallelCopyOsi` 一次复制完整 Q 分量，再执行一次 canonical 缩放和一次粗到细插值；A-B 与 direct 关闭路径继续保留原分批回退实现。
- 说明：`ParallelCopyOsi` 只负责 phase-aware raw 数据搬运，不能替代缩放或插值，因此三阶段仍保持独立。
- 验证：CUDA+MPI 编译通过；作业 `606193` 使用 2 个 MPI rank、2 张 GPU，运行到 step 64 并动态 regrid 到 level 2 后正常结束。`osi_ab` staged check 通过，`AfterAverageDownValid`、`AfterRepair` 和各层 `AfterRefineMeshAllValid` 的有效单元逐点比较均为 `unequal=0`、`linf=0`。日志仍记录了部分 physical ghost 的 `osi_ab_stream_sources` 诊断差异，但 `mismatch_valid=0`；因此本次结果确认了直通初始化路径可运行且有效区域保持一致，不把所有 ghost-source 等价性作为已验证结论。
