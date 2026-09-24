# BOX3D_OSI 第 32 步界面平均后对照

- 目的：在首次重构后的 `AverageDownInterfaceLevel(0, true)` 写回处分别比较 level 0 interface、uncovered 与其余 covered 的 A-B/OSI DDF。
- 修改文件：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/AmrCoreLBM.cpp`、`projects/3Dcases/Computing_performance_test/BOX3D_OSI/docs/current_status.md`。
- 主要改动：增加默认关闭的 `verification.average_probe_first`。启用时，在首次 level 0 界面平均完成后，将全部 valid DDF 按 Fab、q、k、j、i 顺序导出为 canonical double `.bin`，并输出对应的 `.meta` 与每个单元的 covered/interface 字节掩码 `.mask`；OSI 数据按当前 phase 解码。每个 MPI rank 独立写入运行目录。
- 验证：CUDA/MPI 编译通过。相同 inputs 和可执行文件的独立 A-B job 603376 与 OSI job 603377 均正常结束；用仓库根 `.venv/bin/python` 逐值比较 56,623,104 个 DDF 值及掩码。uncovered 49,545,216 值全部相等；interface 1,076,760 值全部不等，最大差 `0.2930981606775104`，位置 `(127,2,126),q=0`；其余 covered 6,001,128 值中 5,057,827 值不等。两边均无非有限值，掩码完全一致。
- 结论边界：此处只确认界面平均后的状态。要判断平均操作是否首次引入差异，仍需在调用前量一次。
