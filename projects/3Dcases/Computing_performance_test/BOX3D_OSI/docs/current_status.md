# BOX3D_OSI 当前交接状态

更新时间：2026-09-10

## 现役配置

权威运行参数是 `config/inputs`：`amr.max_level=2`、`amr.regrid_int=32`、
`amr.blocking_factor_x/y/z=32`、`lbm.stream_mode=0`、`lbm.collide_mode=1`。
当前 `max_step=1000`、`amr.plot_int=1000`，属于短程验证输入；96000 步结果是历史长程
证据，不是这份输入的当前运行结果。
这条路径是 BOX3D 的 A-B 数值对照，不是 OSI 单数组验收。构建使用
`config/GNUmakefile` 指向 AMReX 26.06、CUDA、MPI、C++20 环境；入口为
`./scripts/compile.sh`，提交脚本位于 `scripts/submit_*.sh`。

## 证据边界

- 最新完整作业 `logs/submit/586660-out.log` 正常运行到 step 96000，未观察到 NaN、
  MPI abort 或异常终止；最后一次同相位检查为
  `velocity_l2_relative=5.530030118e-4`、`consecutive=0/3`、`converged=0`，因此只能
  判定运行稳定，不能判定已经收敛。
- 已有阶段 1--7 文档和脚本记录 OSI 单层、两层、checkpoint/restart 及静态粒子验证。
- 当前输入只验证 `stream_mode=0` 的两层级运行稳定性；历史四层诊断不能外推为 OSI
  四层逐单元正确性。
- 静态 `ParticleContainer` checkpoint/restart 已有跨 MPI 分解证据；运动刚体的质心、
  平动/角速度、力和力矩未序列化，因此不能宣称完整 IBM/运动粒子 restart。
- 性能图和旧 job 日志是历史证据，除非脚本在当前源码、输入和硬件上重跑，不得当作
  当前性能基线。
- 当前完整平均下传之后会调用 `RepairCurrentStatePhysicalBoundary()`：A-B 写当前
  `f_old`，OSI 按当前 phase 写 `osi_state`，并且不跳过 covered 物理边界单元。
  `ComputeMacro()` 也采用这条顺序，因此收敛检查和输出前的宏观量计算会先规范化当前
  DDF 边界。该时序已经通过 CUDA+MPI 编译，尚未完成短程场量或 BOX3D 对照。

## 推荐审查顺序

先读本文件和根目录 `README.md` 的“当前状态”，再读
`docs/osi_algorithm_and_architecture.md`、`src/main.cpp`、
`src/AmrCoreLBM.H/.cpp`、`config/inputs`，最后按需运行 `tests/` 下的检查脚本。
修改前遵守仓库根 `CLAUDE.md`/`AGENTS.md` 的基线提交要求，并运行 `git diff --check`。

## 未决事项

当前没有已授权的删除项。物理边界修复的短程数值对照、四层 `stream_mode=0` 的
ghost/边界首次差异仍是 pending；
OSI 多层逐单元验收、动态 regrid 后 restart、运动刚体 restart 和受控性能复测均不在
本状态页的已完成范围内。
