# 新增两 GPU 多层 OSI 冒烟测试脚本

- 目的：直接验证移除持久 `osi_sync_buffer` 后，`osi_mpi_direct=1` 在两 GPU、两 MPI rank 和动态 AMR 场景下可以启动并运行。
- 文件：`scripts/submit_osi_mpi_direct_multilevel_smoke.sh`。
- 测试设置：`gpu=2`、`mpirun -n 2`、`amr.max_level=2`、`max_step=64`、`regrid_int=32`，开启 OSI/A-B 锁步检查和 host-staging direct MPI。
- 验证：脚本语法通过 `bash -n`；job `606170` 实际启动 2 个 MPI rank 和 2 个 CUDA device，运行至 step 64 并完成动态重网格。`osi_ab_stage`、`AfterAverageDownValid` 和 `AfterRepair` 的有效单元逐点检查均为 `linf=0`、`unequal=0`，进程正常到达 `AMReX finalized`。重网格后的 `osi_ab_stream_sources` 仍报告物理 ghost mismatch，但 `mismatch_valid=0`；因此本次证明了 direct MPI 多 GPU 多层路径可运行及 valid 区域锁步一致，未证明所有 ghost source 都逐点一致。
