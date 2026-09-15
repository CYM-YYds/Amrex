朝晖

# CCCM2026 MS04 投稿摘要草稿（可编辑）

## 投稿信息

- 会议：中国计算力学大会 2026（CCCM2026）
- 专题：MS04 计算流体/流固耦合力学
- 交流形式：口头报告（拟）
- 摘要投稿截止日期：2026 年 9 月 20 日
- 投稿通知：[https://mp.weixin.qq.com/s/NuY7YApkjW6mJsNFmGUDfA](https://mp.weixin.qq.com/s/NuY7YApkjW6mJsNFmGUDfA)
- 投稿系统：[https://meeting.cstam.org.cn/?mid=141&amp;sid=561](https://meeting.cstam.org.cn/?mid=141&sid=561)

## 中文题目

单步索引算法在基于 AMReX 的块结构动态自适应网格 LBM 中的实现与 GPU 优化

## 英文题目

Implementation and GPU Optimization of the One-Step Index Algorithm for Block-Structured Adaptive-Mesh Lattice Boltzmann Simulations on AMReX

## 作者与单位

> 请在投稿前补充并确认作者顺序、通信作者和单位编号。

- 作者：[蔡以民]
- 单位：[华中科技大学]
- 通信作者：[柳朝晖]
- 联系邮箱：[m202571330@hust.edu.cn]

## 中文摘要

针对块结构自适应网格（adaptive mesh refinement，AMR）中格子 Boltzmann 方法（lattice Boltzmann method，LBM）存在的离散分布函数双数组存储、粗细网格数据传输及多 GPU 通信开销，本文在 Wang 等基于 AMReX 建立的浸入边界 LBM-AMR 框架基础上，对三维 D3Q27 流体求解核心进行重构与优化，并据我们所知首次将单步索引（one-step index，OSI）算法扩展至块结构动态 AMR。该方法以 AMReX Fab 为局部寻址单元，通过随时间推进的相位地址映射隐式实现迁移，使体积级离散分布函数主状态由 A-B 双数组缩减为单数组。针对多层级、多网块及 MPI 域分解，进一步设计了相位感知的粗到细网格插值与细到粗网格平均、动态重网格数据迁移、物理边界处理、统一逻辑布局的检查点/重启及同层幽灵单元交换机制。为降低 GPU 访存和核函数启动开销，采用预计算相位位移、稀疏边界工作区、精确插值源区、融合式细到粗网格平均以及通信与局部拷贝重叠等优化。三维顶盖驱动方腔测试表明，相较包含动态取模的初始 OSI 实现，预计算相位位移使求解吞吐率提高 61.7%。在双 GPU 单层测试中，2 MiB 分块主机暂存通信相较 AMReX `FillBoundary` 路径的总耗时降低 12.3%～12.5%。结果表明，该实现有效降低了动态 AMR-LBM 的存储和数据搬运开销，为复杂颗粒流的高分辨率、多 GPU 浸入边界计算提供了高效流体求解基础。

## 关键词

格子 Boltzmann 方法；自适应网格加密；单步索引算法；AMReX；GPU 高性能计算

## 研究定位

本摘要将工作的核心贡献定位为：

1. 在基于 AMReX 的块结构动态 AMR-LBM 中实现 OSI 单数组推进；
2. 解决 OSI 在多 Fab、多层级、动态重网格和 MPI 域分解下的相位感知数据寻址与通信问题；
3. 优化 D3Q27 碰撞、物理边界、粗细网格传输和多 GPU 通信路径；
4. 为后续 OSI 与浸入边界颗粒流的完整耦合提供流体求解基础。

当前不将本工作表述为“已经完成 OSI-IB-LBM 颗粒流求解器”，因为现有 OSI 生产路径采用无外力、无 SGS 的 D3Q27 专用碰撞核，OSI 与浸入边界力的完整耦合仍需进一步实现和验证。

## 性能与正确性证据

摘要中的数据来自仓库内现有受控测试：

- 专用 D3Q27 碰撞核：碰撞耗时降低 50.05%，求解吞吐率提高 28.19%；64 步 valid-DDF 对照为 `rel_l2=1.0699e-15`、`linf=1.3878e-15`。
- OSI 相位位移预计算：相较动态取模实现，`MLUPS_solv` 从 407.13 提升至 658.48，提高 61.7%。该结果尚不能外推为多层动态 AMR 的严格逐点等价证明。
- 双 GPU 单层通信：2 MiB 分块 host-staging 相较同作业 `FillBoundary` 路径的总耗时降低约 12.3%～12.5%。
- 周期和六面非周期双 GPU 单层测试分别完成 384/384 次六阶段对照，均为 `linf=0`。

详细记录见：

- `projects/3Dcases/Computing_performance_test/BOX3D_OSI/docs/performance_profiling.md`
- `projects/3Dcases/Computing_performance_test/BOX3D_OSI/docs/current_status.md`

## 投稿前必须核对

1. “首次”应保留“据我们所知”的限定，并在全文或报告中补充系统的 Web of Science、Scopus 或 Google Scholar 文献检索。
2. Wang 等原论文中的 128 GPU 弱扩展效率 97.95% 和 AMR 相对均匀网格加速 18.2 倍属于原论文成果，不能作为本次代码优化的新结果。
3. 当前 Re=1000、160000 步速度剖面对应的检查点布局为 `canonical_ab_two_array_v1`，不能作为 OSI 物理验证结果。
4. 当前 Re=3200、192000 步检查点为 `canonical_osi_single_array_v1`，但尚未达到设定的速度场收敛判据，不应写成“已收敛稳态解”。
5. 多层动态重网格下的 OSI/A-B 严格逐网格等价，以及 OSI 与浸入边界力的完整耦合，仍是正式论文投稿前需要补充的关键验证。

## 主要参考文献

1. Wang, Y., Wu, Y., Zeng, Y., Jiang, M., Liu, Z. An immersed boundary lattice Boltzmann method on block-structured adaptive grids for the simulation of particle-laden flows on CPUs/GPUs. *Computer Physics Communications*, 314 (2025), 109674. [https://doi.org/10.1016/j.cpc.2025.109674](https://doi.org/10.1016/j.cpc.2025.109674)
2. Ma, K., Wang, Y., Jiang, M., Liu, Z. A simple one-step index algorithm for implementation of lattice Boltzmann method on GPU. *Computer Physics Communications*, 283 (2023), 108603. [https://doi.org/10.1016/j.cpc.2022.108603](https://doi.org/10.1016/j.cpc.2022.108603)
