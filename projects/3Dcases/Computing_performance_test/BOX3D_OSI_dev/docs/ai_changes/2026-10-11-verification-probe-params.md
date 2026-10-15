# 验证探针参数集中读取并移出推进函数

- 目的：生产推进路径（`AdvanceLevel`、`AverageDownInterfaceLevel`、`FillGhostLevel`）原先内嵌三类探针，每次调用都新建 `ParmParse("verification")` 或依赖函数内 `static` 计数，且边界探针硬编码采样点 `(1,111,111)` 与 `q=18`。
- 范围：仅 BOX3D_OSI 的 `verification.average_probe_first`、`verification.ghost_probe_first_fill`、`verification.boundary_probe_step` 三个探针。探针默认关闭，不涉及数值核、通信和 AMR 行为。
- 改动：`AmrCoreLBM.H` 新增 `ProbeParams`，由 `ReadParameters()` 一次读取开关与计数，取代三处 `static` 变量。边界探针的函数体搬入 `AmrCoreLBM_diagnostics.cpp` 的 `ProbeBoundarySample()`，推进路径只保留一次条件调用。新增 `verification.boundary_probe_sample`（3 个整数，默认 `1 111 111`）和 `verification.boundary_probe_q`（默认 18，范围检查），默认值与旧硬编码一致。顺带修正旧实现在 level 0 为最细层时访问未分配 `covered_mask` 的问题：`lev >= finest_level` 时按未覆盖输出。
- 验证：`GEN_CCDB=0 ./scripts/compile.sh` CUDA+MPI 编译通过。单 rank / GPU 提交 job `611268`，`max_step=8`、`amr.regrid_int=2`，运行中生成 level 1，三个探针全部开启，分别用 `stream_mode=0/1` 运行：两种布局均输出 `boundary_probe: step=3 ... lev=0`，且读到的 DDF 值一致（`0.018518518518518497`）；平均前后与 ghost 探针文件均生成。
- 验证中发现并修复的问题：首个版本沿用旧实现读取 `covered_mask.at(lev)`，在只有 level 0 的运行中段错误（job `611267`），已改为仅 `lev < finest_level` 时读取。
- 结论边界：没有重跑 64 步锁步矩阵。本次未改动任何推进、通信或数值代码，探针默认关闭，因此未做逐值回归；探针只在单 rank、两层网格（level 0 加 level 1 的 4 个 box）下验证了触发与两种布局的一致性；采样点在 level 0 未被覆盖，`covered=0`，被覆盖点的取值、多 rank 和三层及以上网格均未验证。
