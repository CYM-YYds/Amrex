# AMR正式接入与其他阶段坐标复用筛选

## 状态与正式范围

正式代码只扩展masked OSI碰撞：CUDA三维FLOAT、单MPI rank采用128线程和当前cell九个轴坐标复用；covered/interface筛选、BGK公式、DDF求和顺序、grown-Fab环、phase与同步不变。其他精度/维度、多rank、canonical路径保留原策略。无新增参数或持久缓存。均匀网格原先已接入的无mask路径不变。

lbm.uniform_nomask_collision=0仍选择mask路径，但不再关闭单rank FLOAT碰撞优化；冻结的baseline.ex供原实现对照。正式入口已原子更新，SHA及源快照核对见[production_install.json](production_install.json)。其他阶段候选均为隔离实验，没有装入正式源码或正式程序。构建含用户已有AMR文件/inputs修改，不称干净HEAD验收；用户修改均未纳入本次提交。

## 冻结条件与计时

主轮job621802 SUCCEEDED，节点whshare-agent-185，单A100-PCIE-40GB/单rank；CUDA12.8、GCC11.3、SM80、C++20、RDC、O3/fast_math，FLOAT/D3Q27/BGK/OSI/Re1000。job621798的前三份程序已完整构建后，宏观探针因变量c与已有宏重名构建失败；修复为component后，621802仅续建宏观版本，前三份程序逐字节冻结沿用。失败构建不计入数值/性能样本。

真实FLOAT A5 B checkpoint只读入口：runs/a5_threshold_128000/job621436/B/chk00128000。恢复step128000，固定网格regrid=-1；L0/L1/L2分别1/62/41 Fab、2097152/8110080/20832256 cells，未采用历史DOUBLE的1/10/33布局。性能关闭场输出、checkpoint、收敛与末态检查。每次320 coarse steps，report80，丢弃首80步；三轮顺序为baseline/production/interp/average，反序，average/baseline/production/interp。

每粗步有效更新数95424512，按现有ComputeWeightedLatticeUpdatesPerCoarseStep计算sum(2^lev*(level_cells-next_level_cells/8))。每窗碰撞调用数80/160/320，所有候选各层calls/launch_cells一致。MLUPS按总更新数除累计total，非窗口MLUPS算术平均。total包含推进与重网格，不包含初始化、CheckConvergence和场输出；阶段时间不是孤立kernel纯耗时。

## 正式接入及推进结果

下表毫秒均为每coarse step；范围为三次应用各自稳态均值。

| 实现 | MLUPS_total | total ms | collide ms | interp ms | average ms | total范围 ms |
|---|---:|---:|---:|---:|---:|---:|
| 前轮正式基线（AMR原256/不复用） | 1823.013 | 52.344381 | 34.945845 | 4.216082 | 2.100347 | 52.304818–52.366949 |
| 本轮正式接入（AMR128＋复用） | 2256.053 | 42.297108 | 24.572741 | 4.358675 | 2.160172 | 42.245722–42.334827 |
| 正式接入＋三线性目标坐标复用 | 2256.030 | 42.297540 | 24.556888 | 4.365846 | 2.162842 | 42.292609–42.303443 |
| 正式接入＋半warp平均坐标复用 | 2255.852 | 42.300877 | 24.546092 | 4.358317 | 2.174229 | 42.280342–42.316889 |

正式接入吞吐提高23.754%，total下降19.195%（每粗步节省10.047273 ms）。插值候选相对production的total变化+0.0010%，平均候选+0.0089%，范围重叠，无稳定推进收益，均不接入。

全程allocator峰值：上述12次性能应用的The Arena最大used均9572 MB、最大allocated均30254 MB。allocated是池预留，used是该allocator活跃分配高水位；二者均不是全进程/设备显存总占用。无新增长期坐标存储。探针额外参考数组只存在数值模式，不拿它的峰值替代生产峰值。

## 数值验证

主轮12次全DDF checkpoint比较：production/interp/average/macro × 周期/非周期动态三级fixture及真实三层网格；每层valid/active L1/L2/Linf均有限且精确为0。动态fixture基础32³，8步regrid，80步，实际三级cells32768/110592/262144。真实布局推进32粗步；输出先完成完整平均，再比较前轮独立冻结baseline生成的canonical checkpoint，避免covered粗层的阶段不一致。

宏观量候选另有27次每层密度及三个速度分量逐值比较，最大误差均0。数值gate通过才做性能矩阵。范围限本轮单rank FLOAT，未新增DOUBLE/2D/CPU/多rank或长期物理精度验收；动态AMR做正确性测试，性能采用固定真实布局。

## 宏观量局部测试

宏观量候选每线程读取27分量，预计算九坐标；参考保持原地址表达式及相同浮点顺序。ComputeMacroLevel在非零phase上按原/新/新/原交替，每段连续20次，每层只测一次phase；计时包含该层MFIter发射与末尾GPU同步，排除分配、完整平均、输出及速度差归约。它是局部稳态测试，不是完整推进性能。

| 层/phase | 原 ms/次 | 复用 ms/次 | 局部耗时下降 |
|---|---:|---:|---:|
| L0/32 | 0.374057 | 0.340649 | 8.93% |
| L1/64 | 1.414617 | 1.310008 | 7.39% |
| L2/128 | 3.619347 | 3.314342 | 8.43% |

合计约5.408→4.965 ms，每次全层宏观量计算省0.443 ms。已有输出/收敛调用频率决定总体价值；不能声称推进MLUPS提高8%。保留候选，暂不扩展正式范围。

## 其他OSI访问与资源

- NEE物理边界已经分别复用来源/目标坐标，无需重复接入。
- 三线性插值(mode0)使用canonical coarse staging；仅fine目标需要OSI九坐标。本轮目标核寄存器66→62，但完整interp/total无稳定收益。
- Q27半warp平均每lane只有1–2分量；候选只在当前子单元内复用，寄存器84→88，完整total无稳定收益。
- 宏观量、碰撞和以上相关核均无spill；正式masked碰撞仍168寄存器/线程。静态寄存器变化不证明实际occupancy或硬件瓶颈。
- MPI pack/unpack、canonical encode/decode、CPC平均/插值搬运按分量分配线程，不直接套用每线程九坐标。
- 本地FillBoundaryOsi seam复制每线程遍历27分量，独立补测见下节。插值mode1/2本轮未改动其子单元循环或做新坐标候选测试。

未获得Nsight Compute权限，实际occupancy、DRAM带宽、sector效率和stall未测，不能做唯一硬件归因。

## 本地ghost复制补测

job621808 SUCCEEDED，节点whshare-agent-194，同型号A100、相同工具链。production.ex与主轮正式程序SHA一致；seam.ex只在FillBoundaryOsi本地复制核中复用源与目标各九坐标，不改线程映射、任务、MPI、环范围或搬运数据。周期/非周期动态三级及真实三层共3次各层全DDF比较，valid/active所有范数均为0；通过后在本节点原/新、新/原、原/新交替，每次320步、丢弃首80步。调用和launch_cells一致，更新数仍按实际网格计算。

| 本节点实现 | MLUPS_total | total ms/粗步 | comm ms/粗步 | 三次total范围 ms |
|---|---:|---:|---:|---:|
| 正式接入 | 2223.846 | 42.909668 | 5.460229 | 42.903604–42.920495 |
| 本地ghost坐标复用 | 2235.867 | 42.678972 | 5.358114 | 42.667126–42.685095 |

total下降0.538%（省0.230696 ms/粗步），comm下降1.870%。三轮total范围不重叠，属于稳定但小幅收益；collision等其他阶段也有变化，不能把全部total收益唯一归因于本地copy的纯kernel时间。与主轮节点不同，只比较本节点的配对结果，不能把主轮2256与本轮2236直接作判优。候选暂不接入正式路径。两种性能应用The Arena峰值used9572 MB、reserved30254 MB。局部copy核资源见[资源](job621808/resources.json)；[检查](job621808/checks.json)、[汇总](job621808/summary.json)、[allocator](job621808/memory.json)。

## 复现与保留

沿用scripts/temporary/collision_amr/prepare.py --stage-tests，COORD_STAGE_TEST=1 dsub -s scripts/temporary/collision_amr/submit.sh；COORD_RESUME_JOB=621798可复现本轮续建。补测使用COORD_STAGE_TEST=seam COORD_SOURCE_JOB=621802。实验变量均只由脚本读取，不是正式求解器参数。

保留production_src、各候选源、build/seam_build、基线与各程序SHA、inputs、命令、编译日志、原始日志、checkpoints、图场及JSON。主轮[数值检查](job621802/checks.json)、[性能汇总](job621802/summary.json)、[资源](job621802/resources.json)、[局部宏观量](job621802/macro_probes.json)、[allocator峰值](job621802/memory.json)；原始运行调度脚本保存在各job目录，当前stages.py还可重做离线分析。未删除或迁移历史数据。
