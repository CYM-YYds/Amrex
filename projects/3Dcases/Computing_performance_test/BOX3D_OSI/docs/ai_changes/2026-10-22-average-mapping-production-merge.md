# 按Q合入已验证的批量界面平均映射

## 目的和范围

采纳[16配置比较](2026-10-22-average-batched-matrix.md)的胜出映射，只修改正常OSI的CUDA稀疏界面平均：Q27采用16 lane/父单元，Q19采用单线程/父单元，均为128线程/block。默认格子仍是DIM3/Q27、DIM2/Q9；不新增运行时映射/模型开关，不把实验候选集合或计数状态带入正式类。

## 主要改动

- AmrCoreLBM.H增加编译期OSI线程数、父单元分组宽度及每block父单元数；Q27分别128/16/8，Q19为128/1/128，Q9保留256/32/8。原A-B整warp启动参数与OSI参数分开，避免改变独立canonical对照。
- BuildAverageCache和AverageDownOsiLevel共用同一组OSI常量，标签步长与执行步长一致；继续使用原标签容器、失效/重建流程、当前数组和FabPhaseCache，无新增缓存状态或生命周期。
- Q19直接复用已有average_down_osi_to_canonical标量数学入口。Q27将已验证half_parent数学实现收敛为Kernels.H中的average_down_osi_to_canonical_half_warp和16-lane归约；每lane两槽，保持8子单元顺序、缩放算式、半warp独立掩码及整组尾部退出。源码入口固定ratio2；不引入新的空间插值或共享层。
- 既有整warp函数保留给Q9，A-B启动与数学函数、CPU、全valid平均、插值、边界和通信路径不变。实验tile/映射选择器和验证开关未合入。Q27新内核99寄存器、无spill；未因包装引入前轮90寄存器warp基线偏差。

## 构建与数值验收

正式3D/2D分别通过算例compile.sh构建（GEN_CCDB=0、MAKE_J=8），日志102631/102920已冻结。Q19 job616740使用同一正式新源码，仅在冻结LbmLattice.H将三维Q改为19，全部9个算例翻译单元重编译，复用配置一致的AMReX对象。正式LbmLattice.H和inputs未变。

环境：AMReX26.06、CUDA12.8、GCC11.3、HMPI1.2、FP64、sm_80、A100-PCIE-40GB。

| 模型与作业 | 短程/生命周期逐值对照 | 最大valid Linf |
|---|---:|---:|
| Q27，616739 | 7 | 0 |
| Q19，616744 | 7 | 2.276e-15 |
| Q9二维，616742 | 5 | 0 |

三维mode0覆盖单/双rank×cavity/非均匀种子全周期；mode1/2补双rank周期，各64步、max_level2、maxgrid32、动态fixture。二维保留路径做mode0四种组合。三个模型各做192步双rank缓存生命周期，对照旧程序，日志确认MakeNewLevelFromCoarse/RemakeLevel/ClearLevel；终点层数由checkpoint Header逐层核对，不固定假设始终三层。19组最终有效区及未覆盖区误差均≤1e-12，L1/L2范数有限。

两组三维另做双GPU生产128³网格512步，关闭fixture，按相同阶段写canonical checkpoint，独立CPU逐值对照前轮各自旧基线。Header/Lattice/Fab布局一致：

| 模型 | level0有限Linf | level1有限Linf | level2有限Linf | 未覆盖valid非有限值 | 非有限模式不匹配 |
|---|---:|---:|---:|---:|---:|
| Q27 | 0.0000e+00 | 0.0000e+00 | 0.0000e+00 | 0 | 0 |
| Q19 | 1.9984e-15 | 1.7208e-15 | 1.6098e-15 | 0 | 0 |

原基线level1的covered存储存在非有限值（Q27为5,253,412个分量，Q19为2,987,104个），新旧位置/NaN或Inf类别及Inf符号一致；不将这些条目混入有限差值范数，也不声称全部valid/ghost有限、长期物理稳定或收敛。CPU工具active字段指几何未覆盖valid单元；有限covered值也全部比较。

## 合入后性能核对

每个模型在自己的同节点/GPU作业内，旧基线与正式新代码单rank三轮512步正/反/正比较。Re3200、128³、level0/1/2、BF16、maxgrid128、每32步regrid、interp_mode0、非周期；关闭fixture/输出/收敛/对照及OSI细分计时，整块host-staging。逐窗口各层碰撞调用与launch_cells及平均/插值/边界/缓存计数一致；0的嵌套细分表示未测量。

| 模型、节点 | average旧/新中位秒数 | average配对减少 | 三轮范围 | total配对减少 |
|---|---:|---:|---:|---:|
| Q27，whshare-agent-190 | 3.454241 / 2.655187 | 23.133% | 22.895%–23.201% | 1.938% |
| Q19，whshare-agent-192 | 3.067705 / 1.411545 | 54.000% | 53.965%–54.007% | 5.301% |

完整average含缩放/平均及写回，total含推进中的重网格、不含初始化。本轮Q19加速约2.17×，与前轮不同节点测得的2.53×分别对各自同节点基线成立；不跨节点混合绝对秒数，也不把历史比例当成合入后的固定承诺。Q27约1.30×。不据单GPU计时声称多GPU收益。

## 复现与收尾

[冻结目录及入口](../../runs/average_mapping_merge_20261022/README.md)、[统一验收](../../runs/average_mapping_merge_20261022/audit.json)、[Q27汇总](../../runs/average_mapping_merge_20261022/q27_job616739/summary.json)、[Q19汇总](../../runs/average_mapping_merge_20261022/q19_job616744/summary.json)。52次运行全部成功，19组短程/生命周期对照与2组512步CPU对照通过。正式二维/三维程序SHA256与实际测试的冻结副本一致；其他源文件和生产inputs未改变。

从仓库根执行`.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/average_mapping_merge_20261022/audit.py`重算汇总和指纹核对。保留全部源码、程序、编译命令/日志、输入、运行命令、GPU/主机、checkpoint和唯一分析入口。临时提交入口已有实验目录冻结副本且无外部引用，任务结束删除原scripts/temporary副本，未删除用户工具或运行数据。原CRLF源码行尾保持，diff检查启用cr-at-eol识别。
