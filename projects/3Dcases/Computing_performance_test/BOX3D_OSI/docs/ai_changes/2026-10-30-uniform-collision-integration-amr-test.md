# 单层碰撞优化正式接入与AMR独立测试

目的：接入已证实的均匀网格128线程＋坐标复用，并把128线程、坐标复用及组合纳入保持covered/interface的AMR独立测试。

正式改动仅Kernels.H的BGK编译期缓存选项和AmrCoreLBM_advance.cpp无mask OSI启动。复用既有轴坐标及cached读写helper，不复制公式、不加参数/跨步缓存；仅CUDA三维FLOAT、单rank、固定lev0无mask启用128线程与缓存，其他路径保留旧策略。lbm.uniform_nomask_collision=0沿用原回退。README与当前状态同步。

job621733完成两份隔离CUDA构建、24次数值运行（20次各层valid/active L1/L2/Linf全为0及单层锁步）和21次性能运行。正式单层三轮2822→3680 MLUPS；真实FLOAT A5 B三层布局AMR三轮原路径1826，仅128线程2010，仅复用1899，组合2265；组合total降低约19.4%。完整条件、统计与验证边界见[唯一详细报告](../../runs/temporary/collision_amr/round1/report.md)。

正式单层程序与当前src逐字节对应，已更新算例入口；AMR仅实验，没有把AMR测试分派或新默认策略混入正式源码。动态AMR只作数值回归，性能为固定真实三层网格；不外推DOUBLE、多rank、其他布局或长期精度。用户已有inputs、AMR判据、MLUPS文档及师兄原件未改动或提交。程序含当时用户工作树修改，不称干净HEAD验收。

验证：Shell/Python语法、链接与限定diff检查通过；无新增2D/DOUBLE/CPU构建或多rank验收。报告标明单层旧boundary字段未测量，不把0当无成本。全部复现材料保留，不执行清理。
