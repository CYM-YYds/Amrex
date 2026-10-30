# 宏观量与本地ghost复制坐标复用正式接入

目的：接入前轮已证实有收益的两个候选；复用已有性能和数值证据，接入仅做必要的编译与代表性回归，不重跑性能矩阵。

ComputeMacroLevel在CUDA三维FLOAT、单rank下调用compute_macro_osi<true>，当前cell九坐标供27分量复用。FillBoundaryOsi同范围复用源/目标各九坐标；提取文件内模板发射函数是为了在host编译期分派保留旧路径，同时避开NVCC禁止泛型lambda内定义设备lambda的限制，不引入共享状态。其他精度/维度、多rank保持原寻址；公式、求和顺序、环、phase、任务及同步不变。无参数或跨步缓存。平均、插值、MPI和ParallelCopyOsi未扩展启用。

性能沿用[前轮报告](../../runs/temporary/collision_amr/round2/report.md)：宏观量局部耗时下降约8%，本地ghost复制完整推进耗时下降0.538%；两者原测试相互独立，不累加宣称当前组合加速比。

接入验收沿用collision_amr入口，COORD_STAGE_TEST=integrate选择round3。只做一次成功构建和真实三层网格32 coarse steps的全DDF比较，并比较该次输出速度/涡量的全部Cell_D数据与前轮未接入版本的SHA256。没有重跑动态AMR矩阵、性能矩阵或27次宏观探针。621812首次构建受上述NVCC限制失败，改用模板函数后增量续编；失败构建未产生数值样本。

job621813 SUCCEEDED：三个层级valid/active L1/L2/Linf全部为0，速度/涡量两个PlotFile的六份Cell_D数据SHA256与前轮接入前版本完全一致。正式FLOAT程序已更新为SHA256 88a5fc7d374e9eb38e7a6724696a019ba6372e62c4bd650b09a150cba0c56c32，src与构建快照逐文件相同。资源与候选一致：宏观量158寄存器、本地copy54，均无spill；不据此宣称硬件瓶颈或新组合收益。

Python AST、Shell语法、文档链接及保留CRLF的限定diff检查通过。用户已有inputs、AMR文件和MLUPS文档等修改未提交。未新增DOUBLE/多rank或长期精度验证。源码快照、命令、日志、输入、输出、程序与校验保留在runs/temporary/collision_amr/round3：[检查](../../runs/temporary/collision_amr/round3/job621813/checks.json)、[安装指纹](../../runs/temporary/collision_amr/round3/production_install.json)。
