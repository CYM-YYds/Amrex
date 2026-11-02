# 修正非 Q27 粗化平均线程映射

目的：使 CUDA 非 Q27 标量平均核与外层父单元分配一致，消除 Q9 每 32 线程重复计算并写入同一父单元的配置。

修改 `restriction_group_size` 为 `Q == 27 ? 16 : 1`。复用现有建表和执行共用常量，使 `restriction_parents_per_block` 自动同步；不新增状态或路径。Q9 保留 256 线程/block，Q19 和 Q27 映射不变。影响 BOX3D_OSI CUDA Q9 的完整与界面粗化平均，包括 OSI 和 canonical 寻址；CPU 分支不变。

按用户明确要求不测试：未编译、未运行数值回归，仅检查源码 diff；现有可执行文件未更新，数值验收仍 pending。架构说明同步记录该边界。
