# BOX3D_OSI 当前交接状态

更新时间：2026-09-11

## 现役配置

权威参数在 `config/inputs`：`amr.max_level=2`、`amr.regrid_int=32`、
`amr.max_grid_size=128`、`lbm.stream_mode=0`、`lbm.collide_mode=1`、
`performance.report_int=1000`、`max_step=1000`。默认是 A-B 数值基线；OSI
运行需显式覆盖 `lbm.stream_mode=1`。OSI 地址使用预计算 phase shift；Boundary
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

## 结论边界与待办

上述阶段 oracle 只覆盖单层、单 Fab 的受控 64 步窗口；尚不能证明多 Fab/MPI、非周期
外部 ghost、动态 regrid 下的逐点等价。下一项通信诊断是检查 `CommunicateLevel()` 的
OSI encode/decode tag 是否覆盖下一 phase 的 valid logical box 映射到的 raw 区域，而
不是只覆盖普通重叠 ghost。

当前节点没有 `ncu`/`nsys`，因此尚无硬件内存事务、occupancy 和分支效率计数器。历史
日志、可执行文件、checkpoint 和 `Backtrace.0` 均保留，未执行清理。

## 入口

先读本文件和根目录 `README.md`，再读 `docs/osi_algorithm_and_architecture.md`、
`src/main.cpp`、`src/AmrCoreLBM.H/.cpp` 与 `config/inputs`。性能数字必须同时注明
源码、输入、可执行文件、GPU 和日志；历史 job 不自动等于当前基线。
