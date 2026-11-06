# IBM 插值与铺展核权重复用

目的：参考 PFlows_3D1105.cpp 的可分离权重及插值/铺展复用，减少现有 IBM 的重复核求值。
范围：仅 BOX3D_OSI，二维与三维 force_interp_extrap；保留原四点核、邻域遍历、力公式和原子累加。
权重仅在当前调用内部有效，不新增跨步状态。整层宏观量准备、清零及 SumBoundary 尚未优化。

验证：DIM=2/3 × FLOAT/DOUBLE、单精度粒子、ASan/UBSan 独立原实现对照均通过，
覆盖非均匀密度/速度、负坐标、半格位置、多标记点重叠、逐单元力及标记点力矩。
二维 DOUBLE、三维 FLOAT CUDA 编译通过。

单 GPU 作业 626770：同一 584000 步检查点、固定网格推进32步、三组交错配对。
各组未被细层覆盖的最终 DDF 30864384 个值，最大差均为0。
被覆盖粗层存储有2170个非有限值，分析明确屏蔽并计数；不将其作为有效状态比较。
三组中位数：particle_force 0.473074→0.456463 s（减少3.51%），
particle_total 2.607530→2.569980 s（减少1.44%），total 4.853800→4.820799 s（减少0.68%）。
SumBoundary 未修改，其样本波动不构成优化收益证据。

材料：runs/temporary/ibm_optimization/，复现入口 scripts/temporary/ibm_optimization/。
限制：短测试、固定网格、单 GPU；没有长期、动态重构或多 GPU 等价/性能结论。
后续主要瓶颈仍是整层 SumBoundary，需另行验证局部支撑域与跨 block/rank 累加的完整性。
