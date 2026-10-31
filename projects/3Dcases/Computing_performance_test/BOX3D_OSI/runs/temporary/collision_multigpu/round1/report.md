# 多GPU碰撞128线程＋坐标复用、二维统一默认

## 结论与正式范围

三维同节点2/4 GPU、均匀及真实三层AMR的三对交替结果均有稳定收益，完整推进total下降5.945%–8.106%，均超出各自三次应用的波动范围。已将CUDA FLOAT的masked/nomask OSI碰撞统一为128线程和当前cell坐标复用，移除三维/单rank限制；二维D2Q9沿用相同策略，仅做数值检查，不做性能调优。

covered/interface筛选、BGK公式及求和、grown-Fab环、phase、消息/同步/缓存生命周期不变。CPU、DOUBLE及canonical策略不变；宏观量与本地ghost复制的既有范围不捆绑扩大。无新参数、状态或实验模式进入正式源码。

三维/二维正式程序已更新，[安装及SHA](production_install.json)，[正式源SHA](source_sha256.json)；与两个候选构建源逐文件相同，不重做接入性能/数值矩阵。用户已有六份supervisor日志/pid删除保持原状，不纳入本次提交。

## 固定条件与口径

主作业621978 SUCCEEDED，whshare-agent-185，同节点4张A100-PCIE-40GB，CUDA12.8/GCC11.3、C++20、SM80、RDC、O3/fast_math，FLOAT、D3Q27 BGK OSI、Re1000。每rank独立绑定GPU并记录绑定/退出状态；2rank用2卡、4rank用4卡，不与另一个性能应用并行。主机内host staging、MPI device_direct=0、2MiB分块、碰撞通信重叠保持相同。

三维基线是43aa4ca对应的已验收正式程序，源/程序指纹核对后直接冻结；候选只改变碰撞两个分支的维度/rank限制和编译期发射。固定各rank数内比较候选与基线，不将不同布局单GPU数值混入；这是优化对照，不是扩展效率测试。

- 均匀：256³、max_grid_size=64、lev0、非周期，1200步，report200，剔除首200步。两端保留同样无mask及减少同步策略。
- AMR：只读checkpoint runs/a5_threshold_128000/job621436/B/chk00128000，128³ base、L0/L1/L2全局1/62/41 Fabs、2097152/8110080/20832256 cells；最高level2、固定网格，从128000推进至128320，report80，剔除首80步。
- 每种rank/布局三对换序，第二轮逆序；关闭输出、checkpoint、收敛和末态检查。初始化不纳入total。每个窗口取较慢rank的total，再累加窗口；不相加rank耗时，不平均各rank的MLUPS。
- 有效更新数按当前ComputeWeightedLatticeUpdatesPerCoarseStep全局网格计算；均匀每步16777216，AMR每粗步95424512。各rank/层calls和launch_cells在配对间完全一致；MLUPS分母对每rank每窗口单独核验。
- total包含推进与regrid外围，不含初始化/收敛检查/场输出；重叠阶段时间不独立。均匀减少同步下comm/boundary等0为未测量，不是零成本。下表MLUPS为全域合计吞吐，不是每GPU吞吐；AMR毫秒按coarse step。

## 性能结果

| 配置 | 基线全局MLUPS | 候选全局MLUPS | 基线total ms/步 | 候选total ms/步 | total下降 |
|---|---:|---:|---:|---:|---:|
| 均匀256³ / 2GPU | 2623.186 | 2788.990 | 6.395739 | 6.015516 | 5.945% |
| 均匀256³ / 4GPU | 2999.636 | 3217.885 | 5.593084 | 5.213739 | 6.782% |
| 真实三层AMR / 2GPU | 1374.817 | 1477.493 | 69.408899 | 64.585423 | 6.949% |
| 真实三层AMR / 4GPU | 1576.728 | 1715.820 | 60.520593 | 55.614528 | 8.106% |

| 配置 | 基线三次稳态均值范围 ms | 候选三次稳态均值范围 ms |
|---|---:|---:|
| 均匀256³ / 2GPU | 6.372265–6.439434 | 5.952526–6.065314 |
| 均匀256³ / 4GPU | 5.586413–5.596500 | 5.194111–5.247946 |
| 真实三层AMR / 2GPU | 68.797632–69.826178 | 63.947744–65.215564 |
| 真实三层AMR / 4GPU | 60.483343–60.541819 | 55.557294–55.664887 |

四组范围均不重叠，支持扩大默认范围。结果不外推为跨节点或任意网格的相同比例收益。未获取硬件计数器，不唯一归因为带宽/occupancy；三维优化核168寄存器无spill，与既有单GPU候选一致。完整24次应用结果见[summary.json](job621978/summary.json)。

## 数值与二维验收

有效的全DDF比较共9组，所有相关层valid/active L1/L2/Linf均有限且精确为0：

- 三维2/4 GPU × 单层非周期/全周期，各64³、多Fab、160步；覆盖grown-Fab环相位回绕。
- 三维2/4 GPU真实三层布局，各32个coarse steps，复用已冻结的独立checkpoint；输出完成完整平均后再比较covered粗层，避免阶段不一致。
- 三维4 GPU动态三级fixture，32³ base、max_grid16、BF8、regrid8、80步、周期，复用前轮独立checkpoint。
- 二维2 GPU单层及动态两层，各80步、周期、32² base、max_grid16、BF8；二维基线新构建并生成checkpoint，候选对照同一份checkpoint。不测试二维性能。

首次二维候选构建因copy2保留旧时间戳，未触发advance.cpp重编，其二进制与二维基线相同。主轮这两次候选比较明确排除；三维candidate3d确实单独编译，三维数值/性能数据独立有效。脚本修复为切换源后touch并检查候选/基线指纹不同，补作业621979仅重编二维受影响单元、复用基线checkpoint完成上述两次有效二维比较，未重跑三维性能。

最终二维SHA为58b7947206f07ece096192bd8fa5f71b577a082aad49424c54d17ceeaffbec1d。以[汇总验收](validation.json)及[二维补测](job621979/checks.json)为准，主轮原checks.json中的初次二维比较不作为新候选证明。未验收DOUBLE、CPU、多节点、多种插值新组合或长期物理精度。

## 整次显存与保留

下表为每次应用整次The Arena高水位，先取rank最大值，再列三次应用范围；不是稳态采样点。reserved是分配池预留，used是该allocator活跃分配峰值，不是全进程/全设备总占用。

| 配置 | Arena峰值used MB | Arena峰值reserved MB |
|---|---:|---:|
| uniform / 2GPU / baseline | 1490–1490 | 30254–30254 |
| uniform / 2GPU / candidate | 1490–1490 | 30254–30254 |
| uniform / 4GPU / baseline | 780–780 | 30254–30254 |
| uniform / 4GPU / candidate | 780–780 | 30254–30254 |
| amr / 2GPU / baseline | 5365–5365 | 30254–30254 |
| amr / 2GPU / candidate | 5365–5365 | 30254–30254 |
| amr / 4GPU / baseline | 4319–4319 | 30254–30254 |
| amr / 4GPU / candidate | 4319–4319 | 30254–30254 |

程序报告的整次Total Time（含初始化/报告等，不与稳态total混用）见[整次时间](job621978/application_times.json)。

[原始allocator元数据](job621978/memory.json)、[编译资源](resources.json)、主轮及补测各rank日志/绑定/命令/退出码、源及构建、程序及校验、输入、checkpoint和报告均保留。不清理历史数据。复用collision_amr/submit.sh，COORD_STAGE_TEST=multigpu申请4GPU；补测multigpu-2d申请2GPU，依赖主轮成功以避免干扰性能。临时multigpu.py按rank解析结果，二维没有新增长期性能工具。
