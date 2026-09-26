# BOX3D_OSI：恢复通信回退并固定 host staging 依赖

## 修改目的

修复 OSI 同层通信开关失效、host staging 在不同 GPU stream 间缺少明确依赖，以及当前环境误启用 GPU-aware MPI 配置的问题。

## 修改内容

- `projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/AmrCoreLBM.cpp`
  - `CommunicateLevel()` 根据 `osi_local_direct`、`osi_mpi_direct` 和 MPI rank 数选择 direct 或 canonical 回退路径。
  - `CommunicateOsiLevel()` 恢复 raw OSI 到 canonical staging、`FillBoundary()`、再写回 ghost 的回退流程。
  - direct 入口增加跨 rank 且 `osi_mpi_direct=0` 时回退的保护。
  - pack kernel 完成后再提交 stream 1 的 D2H staging，消除跨 stream 读写竞态。
- `projects/3Dcases/Computing_performance_test/BOX3D_OSI/config/inputs`
  - 当前 HMPI/UCX 环境使用 host staging：`osi_mpi_device_direct=0`。
  - 将 pipeline 块大小设为 `2097152` 字节（2 MiB），避免原来的 1 字节配置。

## 验证

- `GEN_CCDB=0 ./scripts/compile.sh --no-submit`：编译成功，日志为 `logs/compile/compile-20260926T095731-summary.log`。
- 作业 603873：A-B 与 OSI production 各运行 1000 步；OSI 使用 2-rank host-staging pipeline，正常结束且无 NaN/SIG。
- 作业 603875：OSI lockstep，2 rank，direct host-staging pipeline；64 步所有阶段的 `linf=0`，最终 `staged_check=passed`。
- 作业 603876：OSI lockstep，2 rank，`osi_local_direct=0`、`osi_mpi_direct=0`，验证 canonical `FillBoundary` 回退；64 步所有阶段的 `linf=0`，最终 `staged_check=passed`。
- 异步 staging 复测未能提交：当前交互环境的调度客户端无法解析 UID 2542422，且没有生成作业日志；不能据此宣称异步路径已经运行验证。
