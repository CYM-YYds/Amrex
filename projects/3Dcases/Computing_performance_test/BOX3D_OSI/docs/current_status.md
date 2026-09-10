# BOX3D_OSI 当前交接状态

更新时间：2026-09-10

## 现役配置

权威运行参数是 `config/inputs`：`amr.max_level=2`、`amr.regrid_int=32`、
`lbm.stream_mode=1`、`lbm.collide_mode=1`、`performance.report_int=1000`。
OSI 地址只保留预计算 phase shift 路径。
当前 `max_step=1000`，用于 OSI 性能和正确性诊断；A-B 对照必须显式覆盖
`lbm.stream_mode=0`。

## 本轮已验证

- `tests/run_osi_index_test.sh` 通过，覆盖预计算 shift 的周期、方向和置换性质。
- CUDA+MPI 完整构建通过；构建日志为
  `logs/compile/compile-20260910T113452.log`。
- jobs `589418`/`589419` 使用相同 1000 步 OSI 工作量。预计算地址把 Collision 从
  `54.64150041 s` 降到 `18.37013208 s`，约加速 `2.97x`；solver 从
  `93.205 s` 降到 `57.6280 s`。三个 level 的碰撞时间均约下降到原来的三分之一。
- job `589420` 完成单层、非周期 64 步生产 GPU A-B 逐步比较，每一步
  `linf=0`。

## 尚未通过或尚无结果

- job `589421` 在 step 1--31 为 `linf=0`，动态 regrid 创建 level 1 后于 step 32
  触发 `linf > 1e-12` 断言。多层动态 AMR 的第一处分歧仍需逐 level、逐阶段定位。
- 公平的当前可执行文件 A-B 1000 步基线 job `589457` 尚未产生可读取日志，因此不能
  用旧 A-B 数字计算剩余 15% 差距。
- 当前节点未提供 `ncu`/`nsys`，寄存器、occupancy、内存事务等硬件计数器尚未采集。
- 静态粒子 checkpoint/restart 已有历史跨 MPI 分解证据；运动刚体/IBM restart、
  dynamic-regrid restart 和 multi-node 回归不在已验证范围。

## 审查顺序

先读本文件和根目录 `README.md`，再读
`docs/osi_algorithm_and_architecture.md`、`src/main.cpp`、
`src/AmrCoreLBM.H/.cpp` 与 `config/inputs`。性能判断必须同时记录源码、可执行文件、
输入、GPU 和日志，不得把历史 job 当作当前基线。
