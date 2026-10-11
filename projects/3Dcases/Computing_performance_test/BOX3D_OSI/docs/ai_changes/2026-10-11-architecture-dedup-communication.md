# 整合 BOX3D_OSI 通信与有效区转换

目的：去掉同层通信和粗细层 CPC 重复的计划、tag 构建与缓冲状态，同时保持各阶段的执行顺序和数学内核。

范围：仅本算例的 `src/`、锁步日志检查器及直接相关说明。`FillBoundaryOsi()` 直接持有同层通信实现；每层 `FillBoundaryCache` 聚合本地、pack/unpack tag、`MpiPlan` 与 `MpiBuffers`。同层和 CPC 共用 peer offset、tag 构建、缓冲索引及 MPI 请求辅助逻辑；不再保存构建后无消费者的 `RemoteCopyTag`。所有含旧 `MultiFab` 地址的 tag 均在状态交换、重定义或释放前失效。checkpoint 写出和同布局新层初始化共用 valid-only `DecodeOsiValid`，不同布局迁移仍由 `ParallelCopyOsi` 完成。方向表由一份 D3Q27 列表生成 host/device 表；A-B 与 OSI restriction 仅共享 warp 启动和坐标展开，数学、布局读取及累加顺序仍独立。锁步边界只保留 `BoundaryUncovered` 比较，检查器逐步骤、层级、阶段及字段核对完整性。

验证：CUDA+MPI 编译、OSI index、stage-2 A-B 与插值覆盖正负例测试通过。以冻结基线 app SHA256 `947a887ead238225ee46a0d4b873aab95cbe3838634c45664ccad4feb2a1ccb6`、最终 app SHA256 `f2e7d5f5d10d6082193ad0637ee672e0fabb064013af3033e82cebf1731a9d60` 和相同 inputs SHA256 `a23a8f9750899ac6e1f28332c8f060a05423f970128413f920e414a39ffcf84a`，作业 `611228` 完成 9 组 64 步矩阵：动态 AMR 1/2 rank × 插值 0/1/2，周期多 Fab 2 rank × 同步/异步/2 MiB 分块 host staging。每组逐步骤、层级和阶段序列与基线一致（忽略已删除的重复 `Boundary`），所有 uncovered 阶段 `linf=0`；各层 checkpoint valid/active DDF `linf=0`。`AfterRepair` 的 covered `linf=0`；source valid/internal-ghost/interface/covered mismatch 均为零。动态算例仍有既有 physical-ghost source mismatch，最多 734020 个源值；周期组为零，不能将这些检查解释成所有 ghost 全等。作业 `611229` 的 CPC 缓存重建与复用在 1/2 rank 均通过；作业 `611230` 的 phase=0 checkpoint 重启与连续运行 valid DDF 相等。日志、输入和冻结可执行文件保留在忽略的 `runs/architecture_dedup_stage2_final_20261010/`、`runs/cpc_cache_611229/`，历史数据未清理。

限制：本次实际传输是 HMPI host staging；没有 GPU-aware MPI 运行证据。生产 A-B restriction 数学保持独立，锁步矩阵提供其逐阶段对照；未据此宣称长期稳定性或 physical-ghost source 问题已解决。
