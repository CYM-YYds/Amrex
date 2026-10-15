# 移除持久 `osi_sync_buffer`

- 目的：在 `lbm.osi_mpi_direct=1` 的 OSI 生产路径中释放每个 AMR 层长期保留的 canonical 同步显存。
- 修改文件：`src/AmrCoreLBM.H`、`src/AmrCoreLBM.cpp`。
- 主要改动：删除 `osi_sync_buffer` 成员及其初始化/清理；直接 OSI 通信继续使用 raw 地址和 MPI staging；回退通信、粗到细插值、平均、重网格和诊断改用函数内临时 `MultiFab`。回退通信的 canonical `TagVector` 在调用期间现场构造，避免缓存临时 `Array4` 地址。
- 验证：源码中已无 `osi_sync_buffer`、`osi_decode_tags` 和 `osi_encode_tags` 引用；`git diff --check` 通过；`projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/compile.sh` 在 `whshare-agent-1` 上以 CUDA 12.8、MPI 和 `CUDA_ARCH=80` 编译成功，生成 `main3d.gnu.TPROF.MPI.CUDA.ex`。
