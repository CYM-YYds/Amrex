# 新增两 GPU 多层 OSI 冒烟测试脚本

- 目的：直接验证移除持久 `osi_sync_buffer` 后，`osi_mpi_direct=1` 在两 GPU、两 MPI rank 和动态 AMR 场景下可以启动并运行。
- 文件：`scripts/submit_osi_mpi_direct_multilevel_smoke.sh`。
- 测试设置：`gpu=2`、`mpirun -n 2`、`amr.max_level=2`、`max_step=64`、`regrid_int=32`，开启 OSI/A-B 锁步检查和 host-staging direct MPI。
- 验证：提交后记录 job ID、退出状态和每个 rank 的阶段检查结果；脚本语法先通过 `bash -n`。
