# 固定单层正式接入与AMR碰撞因素测试

## 正式启用范围

正式Kernels.H扩展已有BGK模板的编译期坐标复用选项，复用已有osi_cell_coordinates/load_ddf_cached/store_ddf_cached；公式、求和顺序和每Fab grown环语义不变。AmrCoreLBM_advance.cpp只在无mask OSI分支、CUDA三维FLOAT、单MPI rank时采用128线程和坐标复用。其他精度/维度、多rank、AMR masked与canonical路径保留原实现，不新增运行参数或跨步缓存。既有lbm.uniform_nomask_collision=0可回退旧碰撞路径。

正式生产程序已由隔离Makefile构建，并在验证通过后更新算例main3d.gnu.FLOAT.TPROF.MPI.CUDA.ex。SHA256=02b0a9a6375658cc13cb723aafb98fd10ae4fedfaf2e064d81255750e0ab947e，全部正式src与production_src逐字节一致。AMR实验candidate.ex未安装为正式入口。

## 固定实验条件

job621733 SUCCEEDED；节点whshare-agent-50，单A100-PCIE-40GB/单rank，CUDA12.8/GCC11.3、SM80、C++20、RDC、O3/fast_math，D3Q27、FLOAT、BGK、OSI、Re1000。工作树含用户原有inputs/AMR判据修改，均冻结但未纳入本次提交；不称干净HEAD数值验收。

production.ex为正式单层接入程序；candidate.ex只在带covered/interface的OSI分支加入0原256、1仅128、2仅坐标复用、3组合四种host分派。所有分支保留covered/interface判断；GPU不执行测试模式判断。正式masked仍为原256/未复用路径。

## 数值回归

共24次验证应用正常退出，20次独立DDF checkpoint对照的各层valid/active L1/L2/Linf均有限且为0，另有单层16步OSI/A-B锁步正常完成。

- 正式单层：64³单/多Fab、周期/非周期、256³真实尺寸推进320步跨260环长、checkpoint restart；使用前轮独立冻结程序生成的reference。共5次对照。
- 动态AMR：32³基网格、最高level2、regrid_fixture，周期与非周期各跑80步；实际激活level0/1/2，末态valid_cells分别32768/110592/262144。production与四个AMR候选各与冻结程序比较，共10次三层对照。
- 真实AMR：复用FLOAT A5 B的step128000、t1000 checkpoint，固定三层推进32步；各层为1/62/41个Fab、2097152/8110080/20832256个valid cells。production与四候选各层全DDF对比，共5次。

AMR参考与候选均在最终场输出时先完整平均、再写参考checkpoint，避免covered粗层阶段不一致。历史checkpoint只读链接，新checkpoint写本实验local_restart_links目录；没有回写源目录。逐值验收先完成，才启动性能矩阵。

这些验证不等于长期物理精度、其他精度/维度、多rank、多节点或任意AMR布局验收。单层正式接入未扩大到这些范围。

## 单层生产接入性能

256³、单Fab、非周期、静止启动，关闭输出/checkpoint/收敛/末态诊断，保留当前CUDA事件统计。三轮交替，每次1200步，剔除首200步；按累计有效更新量/累计total计算。

| 程序 | MLUPS | total ms/step | 碰撞阶段 ms/step | 三轮total范围 |
|---|---:|---:|---:|---|
| 冻结旧程序 | 2822.377 | 5.944356 | 4.992821 | 5.937574–5.948766 |
| 正式单层接入 | 3680.337 | 4.558609 | 3.589465 | 4.555563–4.562468 |

吞吐提高30.398%，total下降23.312%。这里碰撞为阶段墙钟，不是纯kernel时间；减少同步下原boundary字段为未测量，汇总JSON用null表示，不称边界零成本。

## 真实三层AMR性能

固定上述A5 B三层布局，从同一FLOAT checkpoint重启；关闭regrid、输出/checkpoint/收敛和末态诊断。五种模式三轮换序共15次，每次320粗步，report_int=80，排除首80步。完整total为判据；各窗口各层碰撞调用数及launch_cells逐组一致。MLUPS沿用现有ComputeWeightedLatticeUpdatesPerCoarseStep的去覆盖、按子步加权有效更新量，不把launch_cells或各rank时间相加。

| 模式 | 碰撞线程 | MLUPS | total ms/粗步 | 碰撞阶段 ms/粗步 | 三轮total范围 |
|---|---:|---:|---:|---:|---|
| 正式AMR原路径 | 256 | 1826.463 | 52.245531 | 34.854132 | 52.226141–52.265971 |
| 候选程序原路径控制 | 256 | 1826.148 | 52.254534 | 34.844286 | 52.228227–52.281592 |
| 仅128线程 | 128 | 2010.492 | 47.463269 | 29.895160 | 47.447743–47.475717 |
| 仅坐标复用 | 256 | 1898.800 | 50.255163 | 32.870881 | 50.222769–50.287002 |
| 128线程＋坐标复用 | 128 | 2265.355 | 42.123432 | 24.416110 | 42.120704–42.127832 |

相对正式AMR原路径：

- 仅128线程：吞吐+10.076%，total下降9.153%。
- 仅坐标复用：吞吐+3.961%，total下降3.810%。
- 组合：吞吐+24.030%，total下降19.374%。

原路径控制与正式AMR程序结果接近，候选组合三轮均稳定更快。在这个真实FLOAT布局中，128线程和坐标复用均有独立收益，组合最好；这与历史DOUBLE/不同布局的负结果不矛盾。四种AMR实验核均168寄存器、无stack/spill，不能用寄存器数量下降解释收益。没有硬件计数器，实际occupancy、DRAM吞吐、sector效率和stall未测得。

碰撞减少约10.44ms/粗步，边界等其他阶段仍有成本；不能把小网格或单层的收益率套用到整个AMR推进。当前只是固定真实网格的性能结论，动态regrid只做了数值回归，不据此宣称动态重网格全程加速。

## 去留与复现

本次按“单层先接入、AMR独立测试”执行：正式单层已启用128线程＋坐标复用，AMR候选仍在隔离副本，未扩展正式默认范围。当前证据支持该真实单GPU FLOAT布局的AMR候选，但不推广到DOUBLE/多GPU/其他布局。

临时入口scripts/temporary/collision_amr/{prepare.py,submit.sh,analyze.py}保存生成、同节点构建/运行和数值门禁；production_src/candidate_src、程序哈希、两份编译日志、命令、输入、checkpoint、checks/summary/resources均保留。构建使用算例现有GNUmakefile和匹配缓存，强制重编算例源码；正式安装receipt见production_install.json。小型report/summary及复现脚本纳入提交，大型材料按runs规则留本地。

Python AST、bash -n、文档路径及限定diff检查通过；本轮未新增2D/DOUBLE/CPU构建或多rank测试，相关分支保持旧策略。全部原始运行材料保留，本次不清理。
