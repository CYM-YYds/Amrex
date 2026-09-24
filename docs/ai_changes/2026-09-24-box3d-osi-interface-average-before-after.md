# BOX3D_OSI 界面平均入口、出口及写回路径对照

- 目的：判断第 32 步 interface 差异是否在 `AverageDownInterfaceLevel(0,true)` 调用前存在，并分辨平均写回路径的影响。
- 修改文件：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/AmrCoreLBM.cpp`、`projects/3Dcases/Computing_performance_test/BOX3D_OSI/docs/current_status.md`。
- 主要改动：将默认关闭的 `verification.average_probe_first` 扩展为首次 level 0 界面平均的入口/出口配对导出；每份结果保留完整 valid canonical DDF 与 covered/interface 掩码。
- 验证：CUDA/MPI 编译通过。相同 inputs 和可执行文件的 A-B job 603384、OSI 回退 job 603385 均正常结束；入口 interface 1,076,760 个值全部不等，最大差 `0.004901078005466977`，uncovered 49,545,216 个值全部相等。出口 interface 最大差增至 `0.2930981606775104`，其余 covered 和 uncovered 在本次调用内未变化。两份出口数据与前轮 job 603376/603377 逐字节相同。
- 开关对照：OSI direct job 603386 只将 `osi_parallel_copy` 从 0 改为 1，平均前与回退 job 603385 的全 valid 值相同；平均后仅 interface 有 634,392 个值不同。direct 模式下与 A-B 的 interface 最大差保持 `0.004901078005466977`。源码显示回退路径从稀疏结果拷入 staging 后按整个 `interface_mask` 写回；差异数等于额外 23,496 个粗单元乘 27 个分量，支持回退路径写入超出稀疏源覆盖范围的判断。
- 结论边界：平均前的 interface 差异是真实存在的另一问题；本实验未修复其来源，也未改动 OSI 平均算法。
