# host 按 Fab 取模与 GPU 共享表读取的对照

目的：验证把 ComputeMacroLevel、OSI Collide 和 ApplyPhysicalBoundaryLevel 中的 host 按 Fab 预计算替换为 GPU 共享几何/偏移表读取，是否提升实际推进性能。

范围与结论：只实验 BOX3D_OSI 的上述三个生产路径，不改变共享表更新、通信、通用解码、临时 regrid 迁移或独立诊断。候选没有统一性能收益，因此恢复原源码及基线可执行文件，未保留新生产分支。此前提交 8d77c0a 的通信共享相位表继续使用。

验证：编译节点 SSH 两次拒绝连接，改在作业 613186 的计算节点复用仓库 scripts/lib/env.sh 和 ccdb.sh 的 load_environment/do_compile，仍为 CUDA+MPI、GCC 11.3、CUDA 12.8、CUDA_ARCH=80、MAKE_J=8、GEN_CCDB=0。候选编译成功，作业完成。

- 64 步、三级、regrid_int=32、1/2-rank、三种插值模式的四组对照全部通过：12 个层级结果 valid/active linf=0，4464 条已输出阶段记录相同且 linf=0，28 份平均探针/掩码逐字节相同。结论限于这些检查范围，不扩展为所有 ghost 或长期正确性。
- 性能配置为 2-rank、64³ 基础网格、三级、64 步、blocking_factor=8、mode 0、2 MiB host-staging 流水、关闭锁步诊断及输出；最大 Box 尺寸分别为 32 和 16，每种划分七对，交替改变运行先后顺序。同一对的输入和 rank 布局一致，按每次两 rank 最大的现有 perf(s): total 推进窗口时间比较，包含当前共享表更新成本；不是进程启动至退出墙钟时间。

| 最大 Box 尺寸 | 基线中位数 / s | 候选中位数 / s | 候选变化 | 候选更快的配对数 |
| --- | ---: | ---: | ---: | ---: |
| 32 | 0.2108188324 | 0.2132080688 | 慢 1.13% | 0/7 |
| 16 | 0.3126476746 | 0.3061467285 | 快 2.08% | 5/7 |

两种划分下碰撞中位耗时分别增加约 6.4%/3.8%，边界中位耗时增加约 3.7%/3.2%。总窗口结果依赖划分，短测不支持“去掉 host 取模就一定更快”的结论。

通过 cuobjdump 检查两个实际可执行文件，OSI 宏观量/碰撞/边界 kernel 的寄存器数为 202/198/80 → 206/206/92。原代码每次 host Fab 循环只取模一次并按值传给 kernel；候选增加格点端 GPU 表读取，也改变寄存器需求。寄存器变化是实测事实，不能单独视作性能差异的完整因果证明。

证据保存在 runs/host_context_validation_20261013/：before_src、before.ex、candidate.diff、输入、作业脚本、job613186/owned_src、after.ex、compile.log、可执行文件 SHA256、资源用量及 register_comparison.json。使用仓库根 .venv/bin/python 执行该目录 analyze.py job613186，得到 analysis.json。源码与对应基线快照逐字节相同；用户原有其他修改未纳入本次提交。
