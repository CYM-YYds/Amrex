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

基于 OSI 的自适应网格 LBM 求解器优化/自适应网格 LBM 求解器的 GPU 性能优化

## 英文题目

GPU Optimization of Adaptive-Mesh Lattice Boltzmann Method

## 作者与单位

> 请在投稿前补充并确认作者顺序、通信作者和单位编号。

- 作者：[蔡以民]
- 单位：[华中科技大学]
- 通信作者：[柳朝晖]
- 联系邮箱：[m202571330@hust.edu.cn]

## 中文摘要

### 版本一

块结构自适应网格（adaptive mesh refinement，AMR）方法已用于构建格子 Boltzmann 方法（lattice Boltzmann method，LBM）求解器。已有 AMR-LBM 求解器多采用 A–B 双数组存储方式完成碰撞与迁移，在多层动态网格及 GPU 并行场景下，双数组存储需要维护两套分布函数状态，并涉及数组间的数据交换，从而带来一定的显存占用与数据访问开销。为降低上述存储与数据访问开销，本文在已有基于 AMReX 平台的块结构动态 AMR-IB-LBM 求解器基础上引入单步索引（one-step index，OSI）算法，通过单数组存储与隐式迁移替代传统双数组推进方式，并针对动态 AMR 特性进行适配与优化。针对多层网格中的数据组织与推进过程，对 OSI 索引机制进行扩展，并使其与粗细网格插值与平均、动态重网格及 MPI 通信相协调，实现 OSI 单数组更新方法与原有 AMR-IB-LBM 求解器的耦合。测试结果表明，相较 A–B 双数组模式，优化后的求解器全程平均吞吐率提高 16.6%，累计计算耗时降低 14.2%，峰值设备内存有效使用量降低 36.9%。在保持原有 AMR 网格组织和 GPU 并行执行模式的同时，优化后的求解器有效降低了 LBM 状态存储与数据搬运开销，表现出更高的整体计算效率和更好的资源利用率。

### 版本二（现使用）

复杂流动中的跨尺度局部结构需要精细分辨，而大规模、长时间模拟又受到计算资源和计算时间的限制，因此亟需兼具局部网格自适应能力与高计算效率的格子 Boltzmann 方法（lattice Boltzmann method，LBM）求解器。目前许多 AMR-LBM 求解器采用 A–B 双数组存储方式完成碰撞与迁移，在多层动态网格及 GPU 并行场景下，双数组存储需要维护两套分布函数状态，并涉及数组间的数据交换，从而带来一定的显存占用与数据访问开销。为降低上述存储与数据访问开销，本文在已有基于 AMReX 平台的块结构动态 AMR-IB-LBM 求解器基础上引入单步索引（one-step index，OSI）算法，通过单数组存储与隐式迁移替代传统双数组推进方式，并针对动态 AMR 特性进行适配与优化。针对多层网格中的数据组织与推进过程，对 OSI 索引机制进行扩展，并使其与粗细网格插值与平均、动态重网格及 MPI 通信相协调，实现 OSI 单数组更新方法与原有 AMR-IB-LBM 求解器的耦合。测试结果表明，相较 A–B 双数组模式，优化后的求解器全程平均吞吐率提高 36.6%，累计计算耗时降低 26.8%，峰值设备内存有效使用量降低 36.9%。在保持原有 AMR 网格组织和 GPU 并行执行模式的同时，优化后的求解器有效降低了 LBM 状态存储与数据搬运开销，表现出更高的整体计算效率和更好的资源利用率。

# 关键词

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
- 单 GPU、Re=1000、三级动态 AMR、128000 步完整算例：OSI 与 A-B 的 40 个统计窗口具有相同的网格规模，全程平均 `MLUPS_total` 由 934.699 提升至 1090.556，提高 16.68%；累计计算耗时由 5273.57 s 降至 4522.74 s，降低 14.24%；AMReX 设备 Arena 峰值有效使用量由 14410 MB 降至 9091 MB，降低 36.9%。本次测试未执行终态 DDF 逐点误差比较。
- 周期和六面非周期双 GPU 单层测试分别完成 384/384 次六阶段对照，均为 `linf=0`。

详细记录见：

- `projects/3Dcases/Computing_performance_test/BOX3D_OSI/docs/performance_profiling.md`
- `projects/3Dcases/Computing_performance_test/BOX3D_OSI/docs/current_status.md`
- `projects/3Dcases/Computing_performance_test/BOX3D_OSI/logs/submit/596890-re1000-128k-ab.log`
- `projects/3Dcases/Computing_performance_test/BOX3D_OSI/logs/submit/596891-re1000-128k-osi.log`

## 投稿前必须核对

1. “首次”应保留“据我们所知”的限定，并在全文或报告中补充系统的 Web of Science、Scopus 或 Google Scholar 文献检索。
2. Wang 等原论文中的 128 GPU 弱扩展效率 97.95% 和 AMR 相对均匀网格加速 18.2 倍属于原论文成果，不能作为本次代码优化的新结果。
3. 当前 Re=1000、160000 步速度剖面对应的检查点布局为 `canonical_ab_two_array_v1`，不能作为 OSI 物理验证结果。
4. 当前 Re=3200、192000 步检查点为 `canonical_osi_single_array_v1`，但尚未达到设定的速度场收敛判据，不应写成“已收敛稳态解”。
5. 多层动态重网格下的 OSI/A-B 严格逐网格等价，以及 OSI 与浸入边界力的完整耦合，仍是正式论文投稿前需要补充的关键验证。
6. Jaber 等已将基于 Esoteric Twist 的原位单数组迁移用于 GPU 原生动态 AMR-LBM，因此不能声称本文首次实现“单数组 AMR-LBM”；本文的首次性仅限定为 OSI 向动态 AMR-LBM 的扩展。

## 主要参考文献

1. Wang, Y., Wu, Y., Zeng, Y., Jiang, M., Liu, Z. An immersed boundary lattice Boltzmann method on block-structured adaptive grids for the simulation of particle-laden flows on CPUs/GPUs. *Computer Physics Communications*, 314 (2025), 109674. [https://doi.org/10.1016/j.cpc.2025.109674](https://doi.org/10.1016/j.cpc.2025.109674)
2. Ma, K., Wang, Y., Jiang, M., Liu, Z. A simple one-step index algorithm for implementation of lattice Boltzmann method on GPU. *Computer Physics Communications*, 283 (2023), 108603. [https://doi.org/10.1016/j.cpc.2022.108603](https://doi.org/10.1016/j.cpc.2022.108603)
3. Jaber, K., Essel, E. E., Sullivan, P. E. GPU-native adaptive mesh refinement with application to lattice Boltzmann simulations. *Computer Physics Communications*, 311 (2025), 109543. [https://doi.org/10.1016/j.cpc.2025.109543](https://doi.org/10.1016/j.cpc.2025.109543)
