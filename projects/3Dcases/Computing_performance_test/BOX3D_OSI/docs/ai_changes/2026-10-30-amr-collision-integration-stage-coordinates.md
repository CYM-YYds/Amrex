# AMR碰撞正式接入与其他阶段坐标复用筛选

目的：将前轮单GPU FLOAT AMR候选的128线程与坐标复用接入正式推进，随后独立判断其他阶段是否值得采用。

正式改动仅AmrCoreLBM_advance.cpp的masked OSI碰撞分支：CUDA三维FLOAT、单MPI rank使用128线程与已有BGK模板的坐标复用；covered/interface判断、数学公式、各层相位和同步不变。其余精度、维度、多rank及canonical路径保留旧实现。不新增参数或持久状态；既有uniform_nomask_collision=0只选择mask路径，不再回退旧碰撞线程/寻址，README已同步。

复用现有collision_amr实验入口，通过prepare.py --stage-tests及COORD_STAGE_TEST=1选择round2，扩展stages.py统一冻结、构建、先回归后交替测试。实验源码中分别测试三线性插值(mode0)目标九坐标、Q27半warp平均当前子单元九坐标、宏观量计算九坐标；未将它们写入正式Kernels.H。边界原本已复用，不重复改造；MPI打包/解包与canonical编解码按分量分配线程，不直接套用九坐标。本地FillBoundaryOsi ghost复制仍是每线程27分量，另设独立候选和同节点对照。插值mode1/2本轮未改动或测试新的坐标缓存。

现有perf(s).total截止于推进结束，包含regrid、排除初始化/收敛检查/场输出；宏观量不能套用该分母。宏观量候选在非零phase下独立做每层rho/velocity逐值比较及ABBA重复计时，属于局部阶段证据。

验证：job621802与621808均SUCCEEDED，累计15次逐层全DDF比较的valid/active L1/L2/Linf全为0，另27次每层密度/速度场比较全为0。先通过数值gate，再完成18次性能应用；三个同节点换序对照中，AMR正式接入1823→2256 MLUPS，total下降19.19%。三线性插值与半warp平均无稳定完整推进收益，不接入；宏观量局部每层快7.4–8.9%，本地ghost复制完整total下降0.538%，均保留独立候选，不将局部收益泛化为推进加速。621798仅宏观探针构建因c宏重名失败，修复为component后续建；失败作业不计入样本。

正式FLOAT程序已原子更新为SHA256 ceb3e8849f21e178c8cbbe4bcba1e1a7c25460a41a7ac6994ad3085b18263cc4，当前src与生产快照逐文件一致，实验候选未安装。主轮和ghost补测性能运行的The Arena峰值used均9572 MB、reserved30254 MB，不将池预留混同全设备占用。

Python AST、Shell语法及限定diff/文档路径检查通过；未新增DOUBLE/2D/CPU/多rank、动态AMR性能或长期物理精度验收。输入、用户AMR判据/格式改动及MLUPS文档、师兄原件均保留且未纳入提交。无硬件计数器归因。完整条件、计时边界、波动及保留路径见[详细报告](../../runs/temporary/collision_amr/round2/report.md)。
