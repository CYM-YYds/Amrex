# BOX3D_OSI 当前交接状态

更新时间：2026-09-12

## 现役配置

权威参数在 `config/inputs`：`amr.max_level=2`、`amr.regrid_int=32`、
`amr.max_grid_size=128`、`lbm.stream_mode=1`、`lbm.collide_mode=1`、
`lbm.osi_local_direct=1`、`lbm.osi_mpi_direct=0`、`performance.report_int=1000`、
`max_step=1000`。默认是 OSI 单数组路径；A-B 基准需显式覆盖 `lbm.stream_mode=0`。
跨 rank direct 路径需显式打开 `lbm.osi_mpi_direct=1`；device-buffer 还需显式打开
`lbm.osi_mpi_device_direct=1`，且 AMReX 必须检测到 GPU-aware MPI。OSI 地址使用预计算 phase shift；Boundary
保留坐标缓存，碰撞显式坐标缓存和 branchless 分支均未保留。

## 最新验证

- `tests/run_osi_index_test.sh` 已修正为当前 `OSI` 命名空间，并通过。
- CUDA+MPI 当前源码构建通过；阶段 oracle 作业 `589641`（A-B/OSI、单层非周期、64
  步）在 Initial、Collision、Communication、Stream、Boundary、Swap 全部保持
  `linf=0`。`589648` 的 branchless 试验也通过同一 oracle，随后已恢复 if/else。
- 当前 if/else 生产运行 `589647`：Collision 17.4206 s、Communication 9.2777 s、
  Boundary 2.4880 s、solver 34.2746 s。branchless `589649` 的对应 solver 为
  34.2732 s，差异约 0.004%，且寄存器数由 34 增至 36，不构成收益。
- Boundary 坐标缓存对照 `589544`→`589593` 将 Boundary 从 2.7042 s 降至 2.4847 s
  （约 8.1%），但 solver/total 基本不变；应视为局部 kernel 收益。
- CUDA+MPI 构建 `compile-20260912T121424.log` 通过。job `591180` 在 2 ranks、2 GPUs、
  单层 8 Fab、全周期条件下完成 64 步，六个阶段全部 `linf=0`。
- 同作业性能对照 `591181` 中，A-B 为 4.981--4.989 s、420--421 MLUPS，OSI MPI
  direct 为 5.505--5.517 s、380--381 MLUPS；OSI 慢约 10.8%。通信计划和 staging
  缓冲缓存化、pack/unpack kernel 融合后，相比 `591174` 的约 31.7% 差距缩小约三分之二。
- 本地 seam copy 移到远端 MPI 投递之后，与 MPI wait 重叠。job `591186` 的双 GPU
  64 步六阶段 A/B 全部 `linf=0`；job `591187` 中 A-B 为 5.027--5.035 s，OSI 为
  5.342--5.354 s，OSI 差距进一步降到约 6.1%--6.5%。
- CUDA-aware device-buffer 路径已实现并通过 CUDA+MPI 构建。当前 HMPI/UCX 没有可用
  CUDA transport；job `591185` 强制 device pointer 后由 UCX `process_vm_readv`
  报 `Bad address` 并终止。job `591188` 不再强制能力标志，程序在通信前由
  `UseGpuAwareMpi()` 门禁安全拒绝启动，因此该路径在当前集群的运行验收保持 pending。

## 结论边界与待办

同层跨 MPI direct 已在单层、多 Fab、2 ranks/2 GPUs 的全周期和六面非周期条件下通过
逐阶段 A-B；它仍不能证明多层动态 regrid、restart 或运动 IBM 的逐点等价。当前性能
差距主要位于 host staging 和 MPI wait。当前集群若要继续 device-buffer 直传，需要换用
支持 CUDA-aware MPI 的模块或由平台侧提供 CUDA UCX/BTL；代码不得用
`amrex.use_gpu_aware_mpi=1` 绕过真实能力检测。

当前节点没有 `ncu`/`nsys`，因此尚无硬件内存事务、occupancy 和分支效率计数器。历史
日志、可执行文件和 checkpoint 均保留，未执行清理；当前算例根目录未发现
`Backtrace.0/1` 实体文件，IDE 标签页可能是已删除文件的缓存。

## 入口

先读本文件和根目录 `README.md`，再读 `docs/osi_algorithm_and_architecture.md`、
`src/main.cpp`、`src/AmrCoreLBM.H/.cpp` 与 `config/inputs`。性能数字必须同时注明
源码、输入、可执行文件、GPU 和日志；历史 job 不自动等于当前基线。
