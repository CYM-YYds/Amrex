# 修订累加优化：OSI最后，1000步

目的：按用户反馈恢复累加MLUPS图，修正旧基线未关闭平均批量、单rank通信回退被当成优化和A-B线程开关不生效的问题，OSI放最后，每组从624717/C step128000恢复推进1000粗步。

范围：仅BOX3D_OSI实验副本与现有optimization_ladder入口。使用job626450已验证冻结源码，排除现场用户未提交源码修改。保留此前所有图表及实验，不覆盖历史结果。

主图顺序：A-B基线（平均/插值批量、边界批量关闭，256线程）→平均与插值批量→边界批量薄面/直接构面→碰撞128线程→OSI（含环形坐标复用）。坐标复用依赖OSI环形地址，不能把空操作列在A-B阶段；最后一项包含OSI和其坐标复用，不作纯OSI归因。单rank通信隐藏回退不单列，关闭overlap；碰撞批量仍不采用。

复用平均/插值/边界现有数值实现和开关，扩展A-B寄存器碰撞的128/256线程host分派，不新增持久状态，掩码与公式保持不变。实际源码变更写入实验补丁。每组另做64步动态AMR检查，所有valid（含covered）和active逐值比较，另外补充1000步末态相同范围的比较。

单rank单A100，FLOAT/D3Q27、Re1000、C阈值，动态AMR每32步重构；每100粗步输出一个perf窗口，共10窗，累计有效更新量/累计total计算MLUPS，total含重网格、排除恢复/输出。稳定场出发不代表正式收敛，也不消除短测波动。每组一次1000步，小幅变化不作稳定收益结论。

首轮作业626485，节点whshare-agent-96，编译通过；基线与批量组64步active范数为0，同阶段checkpoint三层全部valid逐值一致（不等值及非有限值均为0），但全文件SHA失败并停止。逐字节定位仅Level1两个Fab的18个内部ghost分量不同，原始失败日志和checkpoint_byte_difference.json保留；不宣称ghost全等。为检验是否传播到有效状态，后续各组补充完整1000步末态逐值比较，输出和比较在推进计时外。

续测626497发现末尾ComputeMacro会再平均covered粗层状态，直接与此前写出的checkpoint比较造成covered区差异（active仍为0），检查按要求停止。已关闭实验末尾额外ComputeMacro，保证比较双方都在checkpoint保存阶段；它位于性能计时外。该阶段差异与ghost字节差异分别记录，不能混为一谈。

作业626498的四组A-B同阶段valid/active逐值检查通过；OSI的active仍完全一致，但尚未完整平均的covered区不同并有无效残留，不能要求该阶段所有covered状态一致。

最终实验在checkpoint保存前统一执行ComputeMacro（完整平均＋宏观量）和ValidateInitializedState，在推进perf计时外进行；关闭末尾再次ComputeMacro，保证完整valid比较双方阶段一致。每组1000步不在中途保存checkpoint，计时不混入该规范化操作。实验副本main.cpp补丁与线程分派一起保留，正式源码不改。最终作业626501（whshare-agent-186）编译通过，5次64步检查＋5次1000步测量全部退出0；两阶段的4组相邻比较三层全部valid（含covered）和active差异均为0，完整平均后的有限值检查通过。十个100步窗口完整，逐窗口逐层碰撞calls/launch_cells及加权更新量与基线一致。

原始材料位于runs/temporary/optimization_ladder/round4_osi_last_1000/（job626485字节差异、job626497阶段差异、job626498 covered阶段诊断、job626501最终实验），保留输入、源码、程序、实际命令、日志、checkpoint、SHA、统计及图表。仅完成后清理复制的build/tmp_build_dir，复现脚本保留。

结果：

| 累加项 | MLUPS_total | 相邻变化 | total/s |
|---|---:|---:|---:|
| A-B 基线 | 1132.07 | +0.00% | 47.979 |
| + 平均与插值批量 | 1500.12 | +32.51% | 36.208 |
| + 边界批量薄面、直接构面 | 1583.12 | +5.53% | 34.309 |
| + 碰撞256→128线程 | 1673.21 | +5.69% | 32.462 |
| + OSI（含坐标复用） | 2429.80 | +45.22% | 22.354 |

最终/基线吞吐2.146倍，总耗时减少53.41%；最后一步包含OSI和坐标复用。见[图表与复现报告](../../data_post_processing/results/optimization_cumulative_OSI_last_C128000_1000/report.md)。Python语法、Shell语法、旧主图七行统计兼容性及文档链接通过；图已目视检查。保留所有诊断轮次，仅清理本轮复制build对象。
