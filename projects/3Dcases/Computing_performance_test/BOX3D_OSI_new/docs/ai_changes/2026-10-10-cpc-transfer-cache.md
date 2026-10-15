# 插值与界面平均统一缓存 CPC 搬运任务

- 目的：消除布局不变时反复构造 OSI 通信 tag、peer 布局及收发缓冲；平均的单 rank 写回也不再逐步查询 Box 交集。
- 范围：仅 BOX3D_OSI 的粗到细 ghost 搬运、界面 restriction 结果写回及相应缓存生命周期。新层初始化的完整粗层临时场、插值/平均数值核和同层通信保持原行为。
- 改动：在 `OsiCommunication.H` 增加 `CpcCache<SourceRaw>`，统一缓存 local/pack/unpack GPU tag、MPI counts/offsets 和 device/pinned 缓冲。`BuildDirectInterpolationCache()` / `BuildAverageCache()` 负责构建，执行阶段只传当前 phase、读取当前 DDF。`RefineMesh()`、`ClearLevel()` 和缓存重建在释放相关存储前使 Array4 缓存失效；临时 `ParallelCopyOsi()` 保留调用内缓冲。取消插值/平均每次搬运的重复布局日志。
- 验证：`GEN_CCDB=0 MAKE_J=8 ./scripts/compile.sh --no-submit` CUDA+MPI 编译通过，最终日志为 `logs/compile/compile-20261010T145218-summary.log`。现有插值覆盖测试的 13 项检查及缺口报错负例通过；新提交脚本通过 `bash -n`，差异检查通过。
- 缓存逐值测试：`tests/cpc_cache_test.cpp` 在 1、2 rank / GPU 下通过两种布局、phase 0/17/41 的重复搬运和重建。raw→canonical 与 canonical→raw 的目标 `linf=0`，周期来源正确，未覆盖 valid 哨兵保持，重复执行的发送缓冲地址保持不变。
- 生产回归：改动前先重新编译并冻结基线。job `611053` 的三线性 1、2 rank 组，以及 job `611056` 的守恒线性/二次插值 2 rank 组均完成 64 步，经历 step 32/64 重网格并生成 level 0/1/2。四组改动前后各层全 valid 和 active DDF 均为 `linf=0`；各 1250 条阶段记录完全相同，其中 70 条 `InterpolationGhost` 检查为零差；首次平均前后数据和掩码逐字节一致，实际覆盖 4096 个 interface 单元。
- 测试脚本边界：`611053` 原脚本额外的 8 步 mode-1 测试尚未生成细层，因“必须检查三个层级”的脚本条件失败，不能算 AMR 验证。已把 mode-1/2 改为 64 步，在 `611056` 完整通过；`611053` 仅采用其已通过的两组三线性结果。
- 证据：`runs/cpc_cache_validation_20261010_1445/` 保存改动前源码、diff、基线 app、独立构建日志和 `summary.json`；`runs/cpc_cache_611053/` / `runs/cpc_cache_611056/` 保存输入、app SHA256、命令、阶段日志、平均探针及 checkpoint。可用 `tests/CpcCache.GNUmakefile` 独立构建，向 `tests/submit_cpc_cache_validation.sh` 提供 `CPC_CACHE_TEST_EXE` 和可选的 `CPC_CACHE_BASELINE_EXE` 复现；`CPC_CACHE_MODES=0,1,2` 选择插值模式。
- 结论边界：以上为本次缓存重构与当前基线的等价性验证，不是所有物理 ghost 或长程 AMR 正确性证明。Stream source 的 `mismatch_valid`、`mismatch_internal_ghost` 为零，已有 physical-ghost 差异保持不变。多 rank 验证使用同节点两 GPU、host staging；未重测 device-direct 或多节点。持续保存跨层缓冲会增加常驻内存，未据此声称实测加速。
