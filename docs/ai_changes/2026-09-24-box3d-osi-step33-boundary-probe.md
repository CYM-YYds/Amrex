# BOX3D_OSI 第 33 步 Boundary 后 uncovered 对照

- 目的：核对第 33 步 `Boundary` 返回后，level 0 uncovered 是否仍有 A-B/OSI 差异。
- 修改文件：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/AmrCoreLBM.cpp`、`projects/3Dcases/Computing_performance_test/BOX3D_OSI/docs/current_status.md`。
- 主要改动：增加可选 `verification.boundary_probe_step`；在对应 coarse step 的 level 0 `Boundary` 后、`Swap` 前，读取逻辑格点 `(1,111,111)`、q=18 的当前 DDF 及 covered 标记。默认关闭。
- 验证：CUDA/MPI 编译通过。相同 inputs 快照的独立 A-B job 603370 与 OSI job 603371 均正常结束；两者 `covered=0`，分别读得 `0.018777451872808406` 和 `0.018585093245395951`，差 `0.00019235862741245544`。单点差异足以证明 uncovered 区域仍不一致，但不代表区域最大差。
- 诊断边界：先前细层锁步参考态在第二个细步产生 NaN，不能用作本次 Boundary 结论；本次采用两次独立运行。
