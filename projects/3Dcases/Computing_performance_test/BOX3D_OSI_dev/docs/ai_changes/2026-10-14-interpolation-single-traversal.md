# 插值缩放与写入合并 Fab 遍历

目的：去除插值阶段独立缩放遍历带来的额外 MFIter 同步。
影响：A-B 与 OSI 共用的 ghost/新层/regrid 插值 helper。

修改 `src/AmrCoreLBM_amr.cpp` 和 `src/AmrCoreLBM.H`：在同一个 MFIter 中按 Fab
执行缩放、插值，保留两个 kernel 的同 stream 依赖；移除只有一个调用者的
`ScaleInterpolationStage`。数学公式、工作 Box 和 apply_scale 语义保持一致。

## 验证

CUDA/MPI 编译成功。作业 613428 为 SUCCEEDED，单 GPU 上顺序运行前后两版，
每版分别以 A-B/OSI、128³、Re=1000、level 0–2、regrid_int=32 运行 3200 步。
关闭 plot/收敛停止，每次在 3200 步写 checkpoint；终态对照在性能窗口后执行。

| 模式 | 修改前 interp (s) | 修改后 interp (s) | 插值降低 | 修改前 total (s) | 修改后 total (s) |
|---|---:|---:|---:|---:|---:|
| A-B | 5.303455 | 4.930872 | 7.03% | 126.656180 | 126.226055 |
| OSI | 5.426333 | 5.063543 | 6.69% | 86.355172 | 86.068516 |

A-B 三层 valid DDF 的 linf/l1/l2 均为 0；OSI 三层 active DDF 的 linf/l1/l2 均为 0。
OSI level 1 全 valid 统计的 valid_l1/valid_l2 为 nan，不能据此宣称 covered 区域
全部有限或全 valid 范数检查通过；其余两个层级全 valid 范数为 0。
补充检查：A-B 和 OSI 各自前后 checkpoint 的 6 个 Level 文件 SHA256 全部相同，
确认层数据逐字节不变（不将此解释为数据全部有限）。
性能结果为单次 3200 步短测，不外推为 128000 步固定收益。

证据位于 `runs/interp_loop_comparison_20261014/`，四个运行目录的 run.log、
checkpoint 和 submit.sh 均保留。原始可执行文件 SHA256：
`ce6d1b7541210a695b235bd57298bf0794dcc550a94e74802d8924e8533e3e80`；
修改后：`745149e807a4feb54295978f56e4e8c881040f03e6fea087ffa5c8c77638e055`。
第一次作业 613427 因脚本没有执行权限未启动计算，修正权限后由 613428 执行。
