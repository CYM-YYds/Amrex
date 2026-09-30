# OSI 完整 Q 分量路径

- 目的：移除 `osi_sync_batch_components` 及所有按 `q0` 分批处理，统一使用完整 Q 分量，减少重复 kernel、FillBoundary 和临时场切换。
- 主要文件：`src/AmrCoreLBM.cpp`、`src/AmrCoreLBM.H`、`config/inputs`、`docs/osi_algorithm_and_architecture.md`、`scripts/submit_osi_full_q_matrix.sh`。
- 主要改动：OSI 解码、canonical 缩放、粗细插值、fallback FillBoundary、interface 写回、重网格稀疏 patch、诊断和 checkpoint 比较均改为一次完整 Q；移除配置项；新增四组两 GPU 矩阵脚本。
- 验证：CUDA+MPI 编译错误 0、链接错误 0。作业 `606200` 申请 2 个 MPI rank 和 2 张 GPU，A-B、OSI direct、OSI fallback、全周期非均匀初值四组均正常结束。direct/fallback 的动态 AMR 运行到 step 64 并生成 level 2；有效区域 A-B 逐点检查 `unequal=0`、`linf=0`。direct/fallback 仍有 physical ghost source 诊断差异，但 `mismatch_valid=0`；周期非均匀初值组的 source diagnostics 全部为 0。
