# 多GPU碰撞128线程与坐标复用、二维统一默认

目的：确认碰撞优化在多GPU的完整推进收益后扩大范围；二维直接沿用三维默认，不单独调优性能。

候选仅修改AmrCoreLBM_advance.cpp的masked/nomask两个OSI启动分支：CUDA FLOAT统一128线程及collide_bgk_register_impl<true,true>，去掉维度和单rank限制；covered/interface、BGK求和、ghost范围、phase、通信/同步及生命周期不变。CPU/DOUBLE及canonical仍沿用旧策略。宏观量、本地ghost复制原适用范围不在本轮变更中。无新参数、模式分派或持久缓存，反而删去两个NProcs分支。

沿用collision_amr提交入口，临时multigpu.py集中处理多rank日志/较慢rank聚合和2D/3D构建，避免复制原单rank实验矩阵。三维当前基线有源码指纹，冻结复用；新构建三维候选。数值gate包含2/4GPU单层边界、真实三层和4GPU动态重网格，性能只比较新扩展范围的同节点2/4GPU单层及真实AMR，三对换序。二维仅单层/动态AMR回归，不做性能矩阵。

本轮初次二维候选因copy2保留旧时间戳而未触发重编，二进制与二维基线相同；该两次候选比较不计入验收。修复为切换源码后touch并校验基线/候选指纹不同，只补编二维受影响源和补做二维回归，三维程序确实重编、三维数据独立有效，不重跑三维性能。

621978和621979均SUCCEEDED。有效全DDF对照9组（3D七组、2D两组）各层valid/active范数全0；3D新范围24次性能应用，较慢rank逐窗聚合且calls/launch_cells一致。同节点2GPU均匀/AMR total分别下降5.945%/6.949%，4GPU下降6.782%/8.106%，三对范围均不重叠。已扩大正式默认并直接安装测试程序，不重做接入矩阵；二维不做性能声明。

正式源码与3D/2D候选源逐文件一致，三维程序SHA为9ea45c3c4d4cbf5cb0b029e856cadbf79fb86d5805cb929ed1b2c9939192bea1，二维为58b7947206f07ece096192bd8fa5f71b577a082aad49424c54d17ceeaffbec1d。Shell/Python语法、链接、diff及源/程序指纹检查通过。无CPU/DOUBLE/多节点或长期精度验收；不外推同比例收益。

原始源、配置、输入、命令、日志、程序、指纹、checkpoint、报告和生成脚本保留在runs/temporary/collision_multigpu/round1；用户已有日志删除不纳入提交。完整条件、较慢rank口径、整次allocator峰值和初次二维排除原因见[报告](../../runs/temporary/collision_multigpu/round1/report.md)。
