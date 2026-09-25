# BOX3D_OSI：限制 OSI 有效平均的写回范围

## 修改目的

修复 `AverageDownOsiValidLevel()` 将平均结果写回整个 coarse valid box 的问题。
该写法会改写没有被细层覆盖的 coarse（uncovered）单元；这些单元不应参与本次
细到粗平均。

## 修改文件

- `projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/AmrCoreLBM.cpp`

## 主要改动

保留 canonical DDF 解码、平均和 OSI 地址写回流程，但用
`coarsen(fine_state.boxArray(), ratio)` 与当前 coarse valid box 的交集限制写回，
只更新细层有效盒对应的 coarse 单元，保留 uncovered coarse 单元的原值。

## 验证结果

- 编译：`logs/compile/compile-20260925T150250-summary.log`，成功。
- 锁步定位：`runs/20260925_150634_job603728/`，第 64 步正常重网格。
  `AfterAverageDownValid`、`AfterRepair` 和 `AfterRefineMesh` 的
  `uncovered_unequal` 均为 `0`；covered 区域仍有差异，符合本次只修复 uncovered
  写回范围的目标。
- A-B 1000 步：`runs/20260925_151236_job603730/`，正常完成到 step1000，未见
  NaN/Inf。
- OSI 1000 步：`runs/20260925_151714_job603732/`，保留
  `osi_mpi_direct=1`、`osi_parallel_copy=1`，仅将当前节点不支持的
  `osi_mpi_device_direct` 设为 `0`，正常完成到 step1000，未见 NaN/Inf。

关闭 OSI direct/parallel 的回退路径测试在 step160 因 GPU 内存耗尽退出；默认
`device_direct=1` 的测试因当前 MPI 未启用 GPU-aware MPI 在初始化断言退出，这两项
不是本次写回范围修复的数值结论。
