# BOX3D_OSI 首次重网格后的 ghost 插值对照

- 目的：检查 step 32 首次建立 level 1 后，粗层插值实际写入的 fine ghost，包括所属 Fab 的面、棱、角区域。
- 修改文件：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/AmrCoreLBM.cpp`、`projects/3Dcases/Computing_performance_test/BOX3D_OSI/docs/current_status.md`。
- 主要改动：加入默认关闭的 `verification.ghost_probe_first_fill`。启用时，在 level 1 第一次 `FillGhostLevel(..., true)` 返回后，将 `interp_direct_fine_boxes[1]` 的 canonical DDF 按 box、q、k、j、i 顺序导出为 `.bin`，并将 work box 与对应 fine valid box 写入 `.meta`。OSI 先按 phase 解码。每个 rank 在自己的运行目录写独立文件。
- 验证：CUDA/MPI 编译通过。独立 A-B job 603374 与 OSI job 603375 使用相同 inputs 快照，均正常结束；两边的 4 个 work box 元数据相同。用仓库根 `.venv/bin/python` 对两个二进制文件的 3,650,400 个 double 逐值比较，非有限值均为 0、不同值为 0、最大差为 0。面、棱、角分类的值数分别为 3,538,944、110,592、864，三类均逐值相等。此结果只针对首次 coarse-to-fine 插值返回后的实际 work boxes，不包括后续通信和 restriction。
