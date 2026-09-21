# BOX3D_OSI 当前交接状态

更新时间：2026-09-21

## 现役配置

权威参数在 `config/inputs`：`amr.max_level=2`、`amr.regrid_int=32`、
`amr.max_grid_size=128`、`lbm.stream_mode=1`、`lbm.collide_mode=1`、
`lbm.osi_local_direct=1`、`lbm.osi_parallel_copy=0`、`lbm.osi_mpi_direct=0`、
`performance.report_int=1000`、`max_step=128000`。默认是 OSI 单数组路径；A-B 基准
需显式覆盖 `lbm.stream_mode=0`。将 `lbm.osi_parallel_copy=1` 仅用于单 MPI rank 的
插值/平均 direct path 验证，多 rank 会回退到原有传输路径。
跨 rank direct 路径需显式打开 `lbm.osi_mpi_direct=1`；device-buffer 还需显式打开
`lbm.osi_mpi_device_direct=1`，且 AMReX 必须检测到 GPU-aware MPI。host-staging 分块
流水通过 `lbm.osi_mpi_pipeline_chunk_bytes` 显式启用，默认为 0；当前已测最佳值
为 2097152（2 MiB）。OSI 地址使用预计算 phase shift；Boundary
保留坐标缓存，碰撞显式坐标缓存和 branchless 分支均未保留。

主提交入口 `scripts/submit.sh` 以 `config/inputs` 为唯一当前工作配置，并在启动前复制
到 `runs/<timestamp>_job<job-id>/inputs`。程序从该独立目录运行，PlotFile 使用 `plt_`
前缀，checkpoint 使用 `chk` 前缀；同目录还记录命令行覆盖、完整命令、Git commit、
可执行文件 SHA256 和运行日志。当前输出间隔为 `amr.plot_int=3200`、
`checkpoint.chk_int=32000`。专项 `submit_*.sh` 仍采用各自的历史工作目录约定，不属于
主入口的运行快照合同。

## 最新验证

- 完整动态 AMR 性能记录已更新到 job `596888`：Re=3200 连续三段运行累计到
  step 288000，最后 1000 步窗口为 `MLUPS_solv=1128.94`、`MLUPS_total=1088.70`。
  `596154`/`596532`/`596888` 各自是 0--96000、
  96001--192000、和 192001--288000 的连续段，单段总耗时不代表
  288000 步全程总耗时。
- Re=1000 单 GPU、三层动态 AMR 的 128000 步 A-B/OSI 对照已由 jobs
  `596890`/`596891` 完成。全程 `total` 累计为 5273.57/4522.74 s；40 个窗口的
  平均 `MLUPS_total` 为 934.699/1090.556，最后 3200 步窗口为 935.75/1086.32。
  该对照未执行终态 DDF 逐点误差比较，不作为多层动态网格严格等价证明。

- CUDA+MPI 构建 `compile-20260914T194426.log` 通过。2 MiB 分块流水的全周期
  job `596145` 和六面非周期 job `596150` 均完成 384/384 次六阶段
  `linf=0`。
- 同节点 1000 步 job `596146` 中，2 MiB 流水将 OSI communication 从
  3.848--3.854 s 降至 3.518--3.529 s，total 从 4.381--4.386 s 降至
  4.045--4.049 s；相比同作业 FillBoundary 的 4.616--4.622 s 快约
  12.3%--12.5%。
- chunk 扫描 jobs `596147`/`596148`/`596149` 分别覆盖 1/4/8 MiB；当前
  单 peer、每 rank 约 16 MB payload 下 2 MiB 是已测最佳值。这是显式性能选项，
  未改为生产默认。

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
  `UseGpuAwareMpi()` 门禁安全拒绝启动。
- 改用 CUDA-aware OpenMPI 4.1.5，并补齐配套 UCX 1.12.1 运行库后，job `595584`
  完成双 GPU 64 步六阶段 A-B，384 项全部 `linf=0`，device-buffer 正确性验收通过。
  job `595585` 中 host-overlap solver 为 4.128--4.140 s，device-overlap 为
  45.896--45.908 s；device 路径的 MPI wait 达 44.846--44.860 s。canonical A-B
  在同一 CUDA-aware 栈上也为 45.519--45.535 s，说明退化属于该 MPI transport，
  不能归因于 OSI 地址或 pack/unpack。

## 结论边界与待办

同层跨 MPI direct 已在单层、多 Fab、2 ranks/2 GPUs 的全周期和六面非周期条件下通过
逐阶段 A-B；它仍不能证明多层动态 regrid、restart 或运动 IBM 的逐点等价。
2 MiB 分块流水在已测单 peer 配置下已超过 FillBoundary，但多 peer、多节点和
多层 AMR 的消息数、chunk 大小与重叠收益仍待重新验收。CUDA-aware OpenMPI 路径已经
证明数值正确，但当前 transport 的 device MPI wait 比 host staging 路径高一个数量级，
不得作为生产性能路径。后续需要排查 UCX CUDA transport、rendezvous 协议和 GPU Direct
能力；代码仍不得在未确认 MPI 能力时用 `amrex.use_gpu_aware_mpi=1` 绕过门禁。

当前节点没有 `ncu`/`nsys`，因此尚无硬件内存事务、occupancy 和分支效率计数器。历史
日志、可执行文件和 checkpoint 均保留，未执行清理；当前算例根目录未发现
`Backtrace.0/1` 实体文件，IDE 标签页可能是已删除文件的缓存。

主提交入口现通过 `RESTART_CHECKPOINT` 接收只读源 checkpoint，并在新运行目录创建
本地 `chk<step>` 符号链接；程序后续仍以本地 `chk` 前缀写出，因此不会回写源目录。

## 入口

先读本文件和根目录 `README.md`，再读 `docs/osi_algorithm_and_architecture.md`、
`src/main.cpp`、`src/AmrCoreLBM.H/.cpp` 与 `config/inputs`。性能数字必须同时注明
源码、输入、可执行文件、GPU 和日志；历史 job 不自动等于当前基线。

## OSI ParallelCopy 阶段性状态（2026-09-21）

候选提交 `21a1bab` 已将单 rank 插值和平均接口的 OSI raw direct copy 接入，并把
Q=27 分量合并为单次 kernel。真实 GPU 作业 `601985`（A-B）与 `601991`（OSI direct）
在相同 128^3、36 步、部分细化输入下，step 32 的插值/平均分别为
`0.267/0.266 ms` 与 `0.314/0.274 ms`；step 36 分别为 `0.727/0.800 ms` 与
`0.706/0.739 ms`。这只支持当前单 GPU 短窗口的阶段性性能结论。

逐 cell/逐 q 正确性 artifact、跨 MPI rank 的 ParallelCopy raw pack/unpack，以及多 GPU
插值/平均 direct path 仍为 pending；`lbm.osi_parallel_copy` 默认保持关闭。详细阶段
设计和证据边界见 `docs/osi_parallelcopy_optimization_plan.md`。
