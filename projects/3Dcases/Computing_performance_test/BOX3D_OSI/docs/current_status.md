# BOX3D_OSI 当前交接状态

更新时间：2026-10-27

当前融合实现以 `a6bd3db` 为已提交基线，机制以[架构说明](osi_algorithm_and_architecture.md#11-静态-coarse-fine-传输)为准。
2026-10-27 只读复核了以下四个作业的调度终态、最新验收报告与程序 SHA256；正式二维/三维程序与
`ab_osi_transfer_complete/candidate` 副本一致。审计期间 `AmrCoreLBM_advance.cpp` 有额外格式修改，
故冻结源码指纹不代表当前工作区逐字节一致；该修改保留且不纳入文档收尾提交。
本次没有重新编译或重跑数值实验。后文日期条目保留各次验收范围，旧阶段的实现描述不代表现役代码。
规则、工作区与保留材料见[本次知识收尾](ai_changes/2026-10-27-transfer-knowledge-closeout.md)。

## 2026-10-26：完整 valid 平均与重网格插值进一步融合

完整 valid/界面平均现在共用建表、启动器和数值核；重网格/正常插值共用任务构建与批量执行器。
两种范围与临时/持久生命周期分别保留，AB 完整平均仍保留 `f_new` 缩放写入。
DIM=2/3 CUDA+MPI 构建通过；620330/620331 均 SUCCEEDED。
320 个 GPU 配置、1/2 rank 各 36 个独立初始化配置及 108 组数值运行通过，
AB/OSI 锁步阶段差异为 0，保存的有限 valid DDF 最大误差 `2.275957200481571e-15`。
固定三层追加检查仍确认原有 OSI covered 非有限模式新旧一致，不宣称全 valid 正确。

单卡、64³、受控动态重网格的四轮 total 配对中位变化为 AB +0.120%、OSI -0.100%，总体基本持平；
RefineMesh 调用区间分别 +2.377%/-12.988%，不是单独 kernel 加速。两 rank 数值测试共享 GPU，未验证两卡。
范围与复现材料见[进一步融合记录](ai_changes/2026-10-26-ab-osi-transfer-complete.md)。

## 2026-10-26：AB/OSI 插值与界面平均共用实现

本节记录第一阶段 `96aa0cb`，已由上节 `a6bd3db` 接续。
当时正常推进的两种布局已共用批量缩放/插值、CUDA 界面平均启动及格子专用内核；
Q27 都用半 warp，按布局选择 DDF 寻址和搬运。完整 valid 平均和重网格临时插值当时尚未融合。
DIM=2/3 CUDA+MPI 构建通过；620242 单卡 SUCCEEDED，108 组数值运行和 160 个独立 GPU 配置通过，
同阶段保存的有限 valid DDF 最大误差 `2.4424906541753444e-15`。
两 MPI rank 在一张 GPU 上运行，尚未验证两卡。

同节点固定 64³ 三层 checkpoint 的四轮交替测量：AB 的 interp/average/total 中位耗时变化为
-59.79%/-73.35%/-16.57%；OSI total 为 +0.046%，基本持平。不是 128³ 生产或动态重网格性能结论。
追加三层作业 620246 的四次应用退出 0，严格分析因既有 OSI covered 非有限值而 FAILED；
只读分区审计证明三层 active、有限 valid 逐值差为 0，非有限类型/位置新旧一致，不能宣称全 valid 正确。
目的、范围、逐层记录、调度终态与复现路径见
[本次变更记录](ai_changes/2026-10-26-ab-osi-transfer-fusion.md)。

## 2026-10-26：碰撞候选实验收尾（未采纳）

正式src/config未加入跳过同层ghost碰撞、TagVector碰撞批量发射或坐标复用开关。
本节仅汇总当次冻结实验；当次收尾时另有5处未提交源码修改，不属于该轮验收。
该工作区描述及历史程序SHA256均不作为当前程序证明，后续融合验收以上方记录为准。

- 小网格32³/max_grid_size=16仅用于阶段验证；性能均采用615364真实三层checkpoint。619072长窗口复测已SUCCEEDED，6次应用均退出0；同节点单卡off/on/on/off、各1600步，剔除首400步后，跳过同层ghost的total仅下降0.0969%，未采纳。完整范围、分层耗时和保留路径见[ghost实验权威记录](ai_changes/2026-10-23-ghost-collision-experiment.md#2026-10-26-真实网格长窗口复测已完成)。
- 数值边界：619072三层active范数为0，L0/L1全valid范数出现NaN；日志标记落在covered且interface=0，但两端非有限模式是否相同/源checkpoint是否已有仍pending，不能宣称全valid等价。
- [原区域/裁剪区域批量候选](ai_changes/2026-10-23-collision-batch-experiment.md)分别慢7.32%/6.38%；[坐标复用候选](ai_changes/2026-10-23-collision-profile-coordinates.md)慢1.33%，均为各自冻结程序的同节点对照，未合入。Nsight因ERR_NVGPUCTRPERM未取得计数器，带宽/occupancy/stall归因仍pending；这些负结果不证明碰撞已达性能上限。

以下旧条目保留各次验收的版本和范围，不自动覆盖后续源码修改。

## 2026-10-22：同层碰撞与通信重叠（现役）

此机制于 `a9f6076` 完成该次验收：先碰撞远端发送源Fab，pack后异步提交其余Fab碰撞，与host staging/MPI重叠。
本地复制/unpack仍排在剩余碰撞之后，phase在通信阶段完成后提交；串行路径与CPC数值路径保留。
机制唯一说明见[架构](osi_algorithm_and_architecture.md#61-整块碰撞与同层传输重叠)，
开关、三维/二维输入差异和计时口径见[README](../README.md#同层通信重叠与计时)。

单节点同作业三轮交替、448步窗口的真实AMR total中位数下降：
生产块大小128时2/4 GPU为5.52%/4.18%，块大小64时为4.91%/4.95%；块大小32和单层无稳定收益。
两端均关闭细分计时，不含默认计时设置变化的效果。
最终同程序开关对照另以2/4 GPU各一组核对，不混入三轮统计。

jobs617192/617193/617233/617234/617235/617236共254次程序运行成功；
38,711条阶段记录Linf=0，三层小网格checkpoint的10,948,608个DDF分量CPU逐值差异为0，
DIM2/3构建和42项维度检查及12组控制通过。物理ghost既有差异保持，不解释成所有ghost等价，
也不代表长期精度、多节点、CUDA-aware MPI或二维性能已验收。
该次验收时正式程序与最终测试副本SHA256一致，完整条件、逐层范围和来源分类见
[实现与验收记录](ai_changes/2026-10-22-osi-collision-communication-overlap.md)。

同日较早的通信候选与保留的 `[q][cell]` MPI缓冲布局见
[缓冲布局实验](ai_changes/2026-10-22-osi-communication-buffer-layout.md)；
已测调度候选与未测方向见[通信计划](osi_mpi_communication_plan.md)。
以下条目为各自冻结版本记录；平均映射继续沿用，但旧程序指纹不代表上述现役程序。

## 2026-10-22：按Q采纳批量平均映射

本节记录当时的平均映射合入；映射继续保留，源码、程序及通信配置由上方现役记录接续。

该次合入的CUDA界面平均按编译期Q选择：Q27半warp16/父单元、128线程/block；
Q19单线程/父单元、128线程/block。默认格子仍为三维Q27、二维Q9，Q9保持旧整warp映射；
A-B、CPU、全valid平均和插值/边界/通信路径不变，没有运行时实验开关。
缓存标签与执行共用新的OSI分组常量，沿用原失效与重建生命周期。

该次二维/三维程序均已重建，Q19同源冻结选择也重编通过。jobs616739/616744/616742共52次运行成功，
19组短程/生命周期DDF对照通过（Q27/Q9误差0，Q19≤2.28e-15）；两组三维双GPU生产网格512步
CPU逐值比较通过，有限值误差Q27为0、Q19≤2.00e-15，covered已有非有限模式保持一致。
同节点三轮average配对耗时减少Q27为23.13%、Q19为54.00%，total减少1.94%/5.30%；
这些数字对应本轮单GPU输入，不替代长期物理或多GPU性能验证。
当时程序指纹与该轮验证副本一致。[合入范围、完整条件与复现](ai_changes/2026-10-22-average-mapping-production-merge.md)。

## 2026-10-22：Q19/Q27批量平均映射复测

以下记录合入前的候选比较；该轮正式源码、inputs和程序保持原指纹，仍为Q27批量warp32、256线程/block。
后续采纳与当前程序以上节为准。
4种映射×32/64/128/256线程，共16配置/Q，最终对独立原始程序同节点三轮512步复测：
Q19单线程/父单元、128线程/block的average中位数1.547100秒，对基线3.916119秒约2.531×；
Q27半warp、128线程/block为2.676819秒，对基线3.487361秒约1.303×，64线程版本基本持平。
旧的“Q27整warp、Q19半warp最优”不能代表当前批量路径和扩大的候选集合。

202次求解器运行全部成功，4410次同源平均检查最大误差2.22e-16；两项候选双GPU生产网格512步逐值回归通过，
Q19有限值Linf≤2.00e-15、Q27为0，未覆盖valid单元无非有限值；各自covered已有非有限模式与基线一致。
Q19总耗时有明显节点漂移，不把total差值全部归因于平均优化。
[完整配置、基线偏差修正、数值范围和复现记录](ai_changes/2026-10-22-average-batched-matrix.md)。

## 2026-10-21：批量插值与界面平均合入记录

本节为当时的合入记录；当前线程映射、程序和采纳状态以上方2026-10-22记录为准。

正式源码已在 `7c50e97` 合入正常 OSI 批量插值和 CUDA 批量界面平均，二维/三维
程序当时均已重新构建。当时两个可执行文件与 job616460 实际运行的冻结程序 SHA256 一致；
不再使用旧二维 job615945 或早期三维测量的程序指纹代表当前文件。
默认模型仍为二维 D2Q9、三维 D3Q27/order=0，生产 inputs 未改变。

机制唯一说明见[粗细层传输](osi_algorithm_and_architecture.md#11-静态-coarse-fine-传输)：
该次合入使正常 OSI 插值批量缩放/插值，CUDA 界面平均保持32 lane/父单元、256线程/block并合并盒启动。
碰撞未批量化；canonical/重网格临时插值和CPU平均保留各自路径；没有正式实验切换开关。

job616460 的二维/三维各12组64步对照，加两组192步新建/重建/清层生命周期回归，
共26组最终有效DDF对照误差为0。生产网格512步另对照的8个checkpoint文件全部位级一致。
同作业单GPU三轮实测：interp/average/total耗时减少中位数为54.38%/42.61%/15.37%；
只对该Re=3200、128³、动态AMR输入成立，不是多GPU或长期收敛结论。
详细命令、条件、逐轮数值与复现材料见[正式合入记录](ai_changes/2026-10-21-batch-transfer-production-merge.md)。

该轮Q19进一步优化仍是独立候选，**当时没有合入正式源码或改变默认模型**。
job616462完成12组短程数值对照、插值/平均各420次逐值检查（最大误差与非有限值计数均为0）；
job616483的512步回归中，仅半warp与两项同开候选各有8个checkpoint文件与基线完全一致。
Q19的covered存储含与基线相同的NaN，不能仅看差值Linf=0就声称所有存储有限。
相对已批量化的Q19基线，半warp平均使average/total再减少23.74%/2.23%；
按q展开三线性插值使interp/total增加15.53%/2.41%，两者同开没有稳定总收益。
该轮仅比较半warp与分量插值，尚未比较单线程和tile，不能据此判定平均的最优映射；
最新候选以上节为准，插值仍沿用现有批量空间线程。该轮证据及适用边界见
[Q19实验记录](ai_changes/2026-10-21-q19-batch-transfer-solver-test.md)。

## 2026-10-21：平均线程映射实验收尾

本节记录批量化之前的线程映射实验；现役实现和Q19后续验证以上节为准。
当时正式三维为 D3Q27，CUDA 界面平均逐盒启动，一个32线程warp处理一个父单元、256线程/block。
实验 `verification.restriction_*` 开关与半warp内核只在冻结快照内，正式源码没有这些开关。

Q19/Q27 的独立微基准 job616311 只给出合成输入下的局部收益；Q19 仅在冻结格子表中选择，
当时未将正式算例改为Q19，也未进行Q19完整推进验收。随后实际路径 job616312 完成30个运行：
210次同源平均检查及四组64步checkpoint比较的最大误差均为0；当前半warp实装在剔除前256步窗口后，
单rank average/total耗时分别增加约2.96%/0.29%，双rank同步host-staging增加约5.10%/0.82%。
该轮因此保留整warp；128线程/block、编译期缩放分派等其他组合当时未完成实际路径验证，不能泛化为所有半warp方案无效。

微基准保留[原始报告](ai_changes/2026-10-21-restriction-q19-benchmark.md)，实际采纳判断与复现入口以
[Q27实际路径报告](ai_changes/2026-10-21-q27-half-warp-validation.md)为准。
实验与失败构建证据均保留，正式源码已恢复并重建；当前三维程序已不是616234/616235冻结程序的同一SHA256文件。

## 2026-10-21：加密覆盖与续跑终态

现役三维配置为 Re=3200、域长 1、基础网格 128³、最高 level 2、BF16、
max_grid_size=128、grid_eff=0.7、每 32 步 regrid，收敛判断开启。
`state_error_6` 的映射仍取编译期最高层号 3；阈值、tag 粗化及 E/F 指标的唯一机制说明见
[架构](osi_algorithm_and_architecture.md#判据打标建块与覆盖统计)。

BF32/BF16 测量 jobs 616234/616235 均从原 job615364 的 step=256000 重启，
在同一 step=259200 正常结束（各 4 GPU、3200 coarse steps）；两层各 100 次 regrid，
共 400 组原始打标与实际覆盖记录的计数交叉检查通过。
终点覆盖放大指标 E（相对原始需求）和 F（相对实际覆盖）为：

| 层级 | BF32：E / F | BF16：E / F | BF32 / BF16 实际覆盖全域比例 |
|---|---:|---:|---:|
| 0→1 | 34.13% / 25.44% | 34.02% / 25.39% | 100% / 100% |
| 1→2 | 118.08% / 54.14% | 50.48% / 33.55% | 100% / 68.66% |

BF16 最细层单元比 BF32 少 31.34%，块数从 64 增至 275；改善主要发生在 1→2。
这是覆盖计数结论，不是精度或总成本收益证明；两作业开启详细诊断，不作 MLUPS 判优。
原始日志、CSV 和可复现脚本见[测量记录](ai_changes/2026-10-21-bf32-bf16-tag-coverage-measurement.md)。

本轮其他续跑的终态已核对：

- jobs 615916/615917/615918（最高 level 3，1/2/4 GPU）及 615919（level 2，单 GPU）
  均因显存不足失败；level 2 双 GPU 的 615925 在 step=256448 重网格申请额外约
  10 GiB/卡时失败。它们不是正常完成或收敛。
- job615927（BF32、level 2、4 GPU）在看到全覆盖后由用户手动终止；调度 FAILED
  对应 terminate 事件，不是显存失败。没有本地 exit_status.json，不补造退出凭证；
  已写出内容的完整性未经本次验证，不作为新重启来源。
- job615992（BF16、4 GPU）只续跑到 step=259200，写速度 PlotFile 时报
  `VisMF::Write failed` 并以 6 退出。`plt_259200` 仅约 44 KB，为残缺输出，不能使用；
  没有新 checkpoint，原 chk00256000 源数据保留。具体 I/O 原因仍 pending，
  不能称为显存耗尽，也不能凭共享盘剩余空间排除配额或其他写入问题。
- job615978 的 512 步诊断及 jobs 616234/616235 的各 3200 步测量正常结束；
  后两次关闭大数据输出避开 I/O 失败，不意味着修复了写入问题。

没有完成本轮原计划的 32000 步续跑。两个最终测量的正式速度残差均约 0.086，
高于 1e-4，连续计数 0/3；正常退出、有限值检查和打标一致性都不是收敛证明。
2026-10-21 收尾时调度查询无运行作业，也未发现本轮采样/监督常驻进程。
本轮六个带显存采样的运行目录中，gpu-memory.csv 均为失效软链，原始显存样本缺失；
峰值 JSON 仍在但不能重算验证。BF 对照的原始求解器日志和 400 行覆盖 CSV 均保留，
该缺口不影响打标/覆盖计数复核。今后保留显存证据须保留软链目标或独立副本，不能只保留链接。
完整终态及保留边界见[本轮收尾记录](ai_changes/2026-10-21-refinement-knowledge-closeout.md)。

显存方面，均匀 512³ 的可运行性取决于速度集、精度、数组布局和 GPU 容量；
本轮 OOM 只限定于相应版本、布局、设备和 regrid 峰值。AMR 同时保存粗层和临时旧/新布局，
不能将其峰值与均匀网格的单套 DDF 常驻预算等同，也不能据此宣称单卡 512³ 普遍不可行。

## 2026-10-21：合并入口复核

现役源码入口为本算例 `src/`；原 `BOX3D_OSI_dev/src/` 已于 `53de250` 删除，历史从Git读取，
IDE开发应打开本目录中的同名文件。合并作业615942/615945与BF测量616234/616235仅代表各自冻结版本；
当前源码已增加加密诊断并合入批量传输，三维配置为BF16。较新二维/三维构建和验收
以本页顶部现役记录为准；不把旧程序指纹或旧回归自动扩展到当前版本。
入口、编辑器及归档核对见[收尾记录](ai_changes/2026-10-21-dimension-knowledge-closeout.md)。

## 2026-10-20：二维兼容合并

BOX3D_OSI_dev 的二维/三维统一源码、维度构建和验证入口已合入本算例。
当前入口见 [README](../README.md#二维与三维统一入口)，具体合并与验证见
[合并记录](ai_changes/2026-10-20-merge-dimension-support.md)。
保留三维 inputs 的 Re=3200、最新 state_error_6 判据和运行监护；二维沿用 dev 输入和判据。
当时保留的相邻dev目录后来已在 `53de250` 删除；历史由Git获取。两维 CUDA+MPI 构建通过；作业 615942 的 10 个三维生产/重启场景和 615945 的 42 个主场景、12 个控制均通过，阶段与 active DDF 最大误差为 0。既有 physical ghost 差异保持，未验证长期收敛或性能。以下较早记录的验证范围均不自动扩大到新版本。


本文顶部记录当前源码的有限窗口证据；后续按日期保留的作业结论仅对当时
冻结版本成立。遇到旧 fallback、已删除函数或“重启待验收”的说法时，以顶部
及[架构文档](osi_algorithm_and_architecture.md)中的现役调用链为准。

## 2026-10-19 中心线提取与绘图分离

`extract_run_centerline.py` 现在只提取、校验并生成 `extraction_report.json`，不自动绘图或选择
Fig18参考；`plot_centerline.py` 独立读取CSV，支持横纵范围、坐标标签、图幅和线点大小。
旧图片与比较报告保留。两步用法见[后处理索引](../data_post_processing/README.md#提取一次独立绘图)，
接口变化及验证见[记录](ai_changes/2026-10-19-separate-centerline-extraction-plotting.md)。
下方此前“保留完整报告契约”的记录为当时行为，当前以本段为准。

## 2026-10-19 参数化后处理整合

`data_post_processing/` 顶层 Python 脚本由11个收敛为5个：中心线、收敛和性能三个
参数化绘图入口，保留导出校验与报告的中心线提取入口，以及内部日志解析模块。
六个旧绘图入口由通用入口承接并删除；两个特殊历史工具移入归档，原始数据与图表未改。
用法见[后处理索引](../data_post_processing/README.md)，逐项映射与验证边界见
[变更记录](ai_changes/2026-10-19-parameterized-post-processing.md)。
中心线点列、误差指标和两个已有导出的完整报告通过前后比较；收敛日志布局、CSV往返与
受限二进制夹具回归通过。这验证后处理行为保持，不增加数值收敛或求解器正确性的证据。

## 2026-10-19 脚本生命周期整理

日常与内部入口已收敛至顶层 `scripts/` 的 10 个文件；历史专项驱动迁入
`scripts/archive/`，独立 CUDA-aware 三件套移入未验收实验区，两种固定网格预设合并到
`tests/submit_osi_fixed_grid_check.sh`。当前入口与用法以
[README 索引](../README.md#脚本入口索引)为准，逐项替换见
[维护记录](ai_changes/2026-10-19-script-directory-lifecycle.md)。
下方历史实验中的旧脚本路径表示当时位置，不作为当前命令使用。
本次没有修改求解器或运行配置，没有启动 CFD，也未删除运行数据；模拟参数检查不能替代数值验收。

## 2026-10-16 退出防护与可视化续跑交接

推荐提交入口为 `./scripts/submit_guarded.sh`，仅覆盖本目录，不自动扩展到
`BOX3D_OSI_dev` 或旧 runs 内复制的脚本。运行配置与限制见
[README](../README.md#付费作业的退出保护)，事故和验证见
[退出收尾记录](ai_changes/2026-10-16-job-exit-guard.md)。

- `613495` 从 `613417` 的 128000 步 checkpoint 续跑至 256000 步，2026-10-15
  18:26 完成；保留 40 份 `ux/uy/uz` 多层速度场、最终 `chk00256000`（level 0–2）。
  实际目录为 `runs/20261015_171801_job613495/`。2026-10-16 手动回收退出挂起分配，
  调度终态 FAILED 不表示这 128000 步没有完成；不据此断言数值收敛或统计充分。
- `614521` 复现应用和 MPI 启动器均退出 0、调度仍 RUNNING；去掉 tee 仍复现，
  随后定位到 `.bash_profile` 的非交互式 LightRead 后台恢复；禁用后
  `614533` 用原始脚本自行 SUCCEEDED（12 秒），详见退出收尾记录。
  `614522/614523` 验证外部监督自动回收，
  终态 FAILED、运行任务数为 0，退出凭证与回收凭证均保留。
- 现役保护默认：完成后内部退出宽限 60 秒、启动器退出后调度宽限 60 秒；
  不再默认设置总运行时限或调度硬时限；可按需显式指定。
- 文档删除已由 `afca3b0` 提交；本次移除失效导航，已有机制指向架构和源码。
  根 AGENTS.md → CLAUDE.md 同源链有效；生成记忆只读，未写入。
- 七项本地生命周期测试和三个 shell 语法检查通过；应用数值路径未改动。
  历史边界独立测试夹具仍引用 `BuildBoundaryWorkBoxes`，与现役
  `BuildBoundaryWorkCache` 不一致，保持 pending，不宣称该测试已适配。

代码/运行证据为 verified-current，文档为 changed-and-verified；原挂起作业进程归属取证、
多 rank 新提交入口验收为 pending，其他算例及结构审查为 out-of-scope。
日志、checkpoint、plot 和实验目录全部保留；本次不执行删除。

## 2026-10-15 OSI 细分计时开关

`performance.osi_detail_timing=0` 可关闭 OSI 嵌套细分计时及其同步，主阶段和数据依赖
同步保留。九组 64 步数值回归前后阶段记录及 checkpoint 相同；关闭细分后的单 GPU
整体变化接近零，双 rank 配对中位数慢约 0.54%，未证明一致加速，最终默认 1 延续原统计。
具体范围、冻结版本和证据见[变更记录](ai_changes/2026-10-15-osi-optional-detail-timing.md)。

本轮收尾以 `13e7bda` 加现场函数重命名为准：当前源码、生产 app 和 inputs 与
`runs/osi_timing_20261015/final_src/`、`final.ex`、`final_inputs` 一致。
`613485` 数值矩阵、`613486`/`613487` 性能实验、`613490` 最终默认值调整后的
双 rank 开关验收均为 `SUCCEEDED`；默认仍为 1，开关 0/1 的阶段和 checkpoint 对照均通过。
代码及上述有限窗口运行证据、根规则链为 `verified-current`；现役说明为
`changed-and-verified`。记忆 `generated-read-only`，未写入；部署 `not-applicable`。

以下为 2026-10-15 当时的工作区快照，后续状态以上方 2026-10-16 记录为准：

- 用户未提交的 `BuildBoundaryWorkBoxes` → `BuildBoundaryWorkCache` 重命名已进入本轮
  构建，但永久 `tests/build_boundary_batch_test.sh` 和夹具仍使用旧名，不能将该入口
  描述为在当前 dirty 工作区可直接运行。此前独立测试使用实验目录内适配脚本；
  后续正式集成重命名时需同步测试入口，本次不代为提交用户代码。
- 用户待提交删除 `DDF粗细网格填充学习文档.md`、`osi_performance_profiling.md`，
  导致 README、架构和状态页共七个链接暂不可用；保留引用与删除现场，未恢复或代为提交。
  `MLUPS记录.md`、`osi_mpi_communication_plan.md` 的用户修改也未纳入收尾。
- 长程严格、多节点、GPU-aware MPI、运动 IBM 完整重启和既有 physical-ghost source
  差异仍未闭合；本次没有重新做数值或性能实验。其他算例、结构审查及记忆写入为 `out-of-scope`。

复核现场继续保留：`runs/osi_timing_20261015/` 约 14 GiB；
`boundary_latest_perf_20261015/` 约 2.8 GiB（失败现场），
`boundary_repaired_perf_20261015/` 约 5.3 GiB（成功复测）；
`boundary_direct_faces_20261015/`、`boundary_full_faces_20261015/` 各约 208 MiB。
新增可再生成的清理候选仅为后两个目录的 `unit_build/tmp_build_dir/`，各约 71 MiB；
加上上轮尚未清理的两份同类候选，共约 284 MiB。未删除任何文件，须汇报后确认才清场。

## 2026-10-15 批量薄面边界收尾

后续按用户要求扩大到当前 Fab 的全部域内物理面，见
[域内整面记录](ai_changes/2026-10-15-boundary-full-faces.md)。以下配对实验对应扩大前的冻结范围。
恢复 Fab 范围检查后重新编译的性能作业 `613478` 成功，当前整面与上一版批量薄面
整体耗时差异接近零，见[修复后配对记录](ai_changes/2026-10-15-boundary-repaired-performance.md)。

本节冻结版本由 `a46d4d1` 的批量调度和 `347db9c` 的薄面裁剪组成；
后续 `1f61f85` 改为直接构造薄面，`6f4c5f6` 扩大到全部域内物理面。
状态所有权、失效时机与写入范围统一见
[架构文档](osi_algorithm_and_architecture.md#101-完整平均后的边界修复)。
2026-10-14 的逐盒薄面候选曾回退，不能据其结论判断当前批量实现。

- `613458` 验证批量厚盒，`613463` 验证最终薄面，均为 `SUCCEEDED`。
  两轮各完成六组 64 步回归：三种插值单 rank 动态 AMR、两 rank 动态 AMR，
  以及混合周期/全周期固定单层。前后阶段记录内容及 checkpoint 文件相同，
  阶段完整性检查通过；MPI 日志比较保留重复次数、允许跨 rank 输出交错。
  独立测试每轮在 1/2 rank 各通过 432 组，比较全部 grown 存储和 27 分量，
  全部有限且 `linf=0`。生产 checkpoint 相等仅覆盖保存的 valid DDF，
  不扩展为全部生产 ghost 等价；没有覆盖混合周期动态重网格的历史基线失败。
- `613457`/`613464` 为已成功的单 GPU 配对性能实验。批量调度收益明显，
  进一步薄面裁剪仅有小幅 Boundary 收益，整体改善很小；数值和性能条件分别见
  [批量记录](ai_changes/2026-10-15-boundary-batch.md)、
  [薄面记录](ai_changes/2026-10-15-boundary-batch-thin.md)。
  两轮节点不同，不提供跨轮累计加速数字。
- 截至 `347db9c` 的收尾复核：当时源码、生产 app 与薄面冻结候选一致；代码、冻结运行证据、
  根规则链为 `verified-current`，文档为 `changed-and-verified`。
  记忆为只读 `generated-read-only`，没有写入；部署为 `not-applicable`。
  长程严格、多节点、GPU-aware MPI、运动 IBM 完整重启和历史 physical-ghost
  source 问题仍为 `pending`；其他算例、结构审查、重新性能测量为 `out-of-scope`。

工作区保留用户原有 `docs/MLUPS记录.md` 空行修改，未纳入收尾提交。
本轮三个实验目录 `runs/boundary_faces_20261014/`、`runs/boundary_batch_20261015/`、
`runs/boundary_batch_thin_20261015/` 分别约 7.5/8.3/8.3 GiB，含源码、app、输入、
日志和 checkpoint，继续作为复核证据保留。
仅将后两目录中各约 71 MiB 的 `unit_build/tmp_build_dir/` 列为可再生成的清理候选；
未删除任何文件，须在用户看过汇报并确认后才清场。该候选不包含 unit app 和构建日志。

## 2026-10-14 直接重构插值与诊断范围

现役 `RemakeLevel()` 按 FPinfo 缺失 valid patch 与目标 BoxArray 求交，建立
work_boxes/fine_indices，并调用带区域参数的 `InitializeDdfFromCoarse()`。
插值直接写目标 phase=0 的 valid；没有 fine_patch 中转或最后一次复制。
coarse staging 仍按目标 Fab owner 建立。省略区域参数时初始化整层 valid。
机制统一见[粗细层传输说明](osi_algorithm_and_architecture.md#11-静态-coarse-fine-传输)。

`ParallelCopyOsi()` 有临时 MultiFab 搬运和缓存搬运两个重载；插值/平均使用
缓存重载统一分派本地/MPI，复用原 tag、plan 和 buffer，临时迁移保留调用内计划。
退役的 `InterpolateOsiFinePatchFromCoarse` 已合入 RemakeLevel。

`356a845` 的冻结 app 在作业 613414 完成四组 64 步动态 AMR：mode 0 单/双 rank、
mode 1/2 双 rank，包含 step 32/64 重构。33 个改动前后 DDF checkpoint 文件
逐字节一致，平均探针一致；构建通过。运行包含当时用户未提交的 amr.cpp 修改，
不能将此 app 等同于干净提交构建。详见
[直接写回记录](ai_changes/2026-10-14-direct-regrid-interpolation.md)。

诊断范围按[阶段诊断 skill](../../../../../.agents/skills/stage-aware-amr-diagnostics/SKILL.md)：
普通推进粗检先比较 uncovered valid；重构返回处才专门检查全 valid，包括
covered/interface；插值和平均检查实际读写集合。推进结束后的全 valid checkpoint
比较是存储回归检查，不替代重构测点，也不作为一般 uncovered 粗检的验收条件。
`submit_cpc_cache_validation.sh` 仍执行专项全 valid checkpoint 范数检查；本次未改脚本。

613414 日志的部分全 valid 差值 L1/L2 为 NaN；未逐点定位，不能直接归因于
covered，也不能由 Linf=0 推断所有 valid 有限。字节一致支持存储未改变，
不支持数据全部有限的结论。一般粗检不把这一未分类范数当作 active 流场失败。
若需查因，另开按阶段、范围和非有限值分类的诊断。

本次知识收尾：代码/规则 verified-current，文档 changed-and-verified；运行证据
verified-current 仅指上述冻结窗口，用户后续未提交源码修改的编译/运行 pending。
记忆 generated-read-only，未写入；部署 not-applicable。其他算例、性能重新测量、
长期/多节点/运动 IBM 验收 out-of-scope；既有 physical-ghost source 差异保留。

新增现场全部保留：`runs/cpc_cache_613414` 约 3.7 GiB，含不可丢弃的验证证据；
`parallel_copy_entry_20261014`、`direct_remake_20261014` 各约 119 MiB，含基线 app；
`regrid_interp_reuse_20261014`、`inline_regrid_patch_20261014` 仅约 276/328 KiB 的编译日志。
后续用户于 2026-10-14 在清单汇报后确认清场：重复执行文件硬链接去重、
job596888 中间 PlotFile 清理已完成，合计约 16.12 GiB。
保留最终输出及 checkpoint，其他运行目录未删除，明细见
[清理记录](ai_changes/2026-10-14-runs-dedup-cleanup.md)。

## 2026-10-14 新层初始化与插值收尾

现役 `InitializeDdfFromCoarse()` 仅插值 fine valid，按所需 stencil 建立调用内
coarse staging，不复制整层 coarse，不初始化 fine ghost。coarse stencil 的物理
延拓仍保留。初始化、普通 ghost 填充和重构迁移使用相同自定义数值插值核；
AMReX 的 `DdfInterpolater()` 只提供几何。公式说明统一见
[粗细层传输说明](osi_algorithm_and_architecture.md#11-静态-coarse-fine-传输)。

- `97d998e` 的冻结验证作业 613389：72 组初始化 valid oracle 最大差
  `2.220446049e-16`，全部非 valid sentinel 未改变；测试 stencil 暂存 648
  单元，parent valid 32768 单元。四组 64 步动态 AMR 共 4464 条阶段检查
  `linf=0`。mode 0 改动前后全 valid checkpoint 零差，mode 1/2 独立
  canonical/OSI 的 active 零差，covered 粗层仍约 0.00931062/0.009269。
  详见[valid-only 记录](ai_changes/2026-10-14-valid-only-new-level-initialization.md)。
- `7b4f177` 将单调用者 `BuildDirectInterpolationCache` 原样合入
  `BuildInterpolationCache()` 的逐层循环，未改变计划、状态或失效条件。
  按用户要求只做 CUDA+MPI 编译验证；历史日志路径为
  `runs/merge_interp_cache_20261014/production_build.log`，当前工作区未保留，无法复核。
  作业 613392 已取消，部分日志不计作完整数值回归。此前作业不是该提交的运行证据。
- 稀疏化与自定义公式切换的历史过程分别见
  [稀疏初始化](ai_changes/2026-10-13-sparse-new-level-interpolation.md)和
  [自定义插值](ai_changes/2026-10-14-custom-ddf-interpolation.md)；其中旧的
  AMReX 数值初始化和 valid+ghost 范围只代表当时版本。

上一轮知识收尾（对应当时源码）：代码与根规则链 verified-current；文档 changed-and-verified；
运行证据限于上述冻结作业和当前合并的编译验证；记忆 generated-read-only，
其中旧 builder 名称已过期，以本节和现役源码为准，未写记忆。
长期严格、多节点、GPU-aware MPI、运动 IBM 完整重启仍 pending；
非周期 physical-ghost source 差异未消除。其他算例、性能重新测量与全仓库
结构审查 out-of-scope；没有部署环节，部署验收 not-applicable。

2026-10-14 用户在收尾汇报后授权清理冗余运行：已删除三轮初始化测试构建
目录内的 `build/tmp_build_dir`，保留测试 app、源码及编译日志。
已取消的 `runs/sparse_initialization_613392/` 归并到
`runs/merge_interp_cache_20261014/cancelled_job613392/`：保留独有输入、源码、
日志与 `after.ex`；重复的 before/test app 经 SHA256 确认后删除，其保留位置
记录于归档中的 `retained_executables.json`。合计移除约 500 MiB 文件内容。
613335/613372/613389 成功证据和 613319/613324/613371 失败诊断现场均未删除；
其余历史运行未作冗余判定，继续保留。

## 2026-10-11 当前数值验证边界（冻结版本）

提交 `c358a45`、`789bb2c`、`efe5416` 分别完成无调用路径清理、通信与转换整合、
实验脚本与收敛解析整合。作业 `611228` 使用冻结输入、BoxArray、rank 布局和
可执行文件，完成 9 组 64 步矩阵；每步每层 uncovered DDF 与各层 checkpoint
valid/active DDF 对基线均为 `linf=0`。`AfterRepair` covered 差为零；source
valid/internal-ghost/interface/covered mismatch 为零，但非周期动态算例的
physical-ghost source mismatch 最多 734020 个源值，不能算作全 ghost 通过。
作业 `611229` 的 CPC 复用/重建通过，`611230` 的 phase=0 OSI DDF 重启与连续
运行 valid DDF 相等。CUDA+MPI 构建、OSI index、stage-2 A-B 与插值覆盖正负例
通过。完整 SHA256、输入和运行矩阵见
[通信与转换记录](ai_changes/2026-10-11-architecture-dedup-communication.md)。

以上是 64 步、指定布局与 host-staging 环境下的证据。多节点、长期严格逐值、
多层周期动态 AMR、GPU-aware MPI 和运动 IBM 完整重启仍待独立验收；
不能把已有 physical-ghost 差异当作已修复。

## 2026-10-11 实验脚本与收敛绘图入口

现役专项脚本共用 hostfile/MPI 启动辅助逻辑，64/128/256 步锁步参数
从同一配置函数生成；旧 direct 参数已从入口移除，但程序仍拒绝 `=0`。
两个历史收敛绘图入口共用严格日志解析和 step 去重，并支持显式输入/输出路径。
模拟启动器检查了参数及失败传播，历史日志在独立验证目录重画通过；
细节见[脚本与后处理记录](ai_changes/2026-10-11-architecture-dedup-scripts.md)。

## 2026-10-11 架构去重

现役 OSI 同层通信任务按层保存在 `FillBoundaryCache`，与 CPC 共用
`MpiPlan`、`MpiBuffers` 和 tag 构建逻辑；checkpoint 与同布局新层初始化
统一解码 valid DDF。输入模式由 `RunMode` 唯一保存，方向表从同一列表生成。
锁步诊断移除重复的 `Boundary`，保留 `BoundaryUncovered`；四套旧推进函数
和三套 cumulant 数学内核未改。当前验证矩阵及限制见
[架构去重记录](ai_changes/2026-10-11-architecture-dedup-communication.md)。

## 2026-10-10 插值与界面平均搬运缓存

OSI 的粗到细 ghost 搬运和界面平均写回均在缓存构建阶段准备 CPC 派生的本地、
发送和接收任务、peer 偏移及收发缓冲；推进时复用任务，并传入当前 phase。
重网格或清层前使含 Array4 的任务失效，布局建立后重新获取数据地址。
临时重网格复制仍使用调用内缓冲。

单/双 rank 三线性及双 rank 守恒线性、二次插值的 64 步对照，改动前后各层
全 valid DDF 均为零差，平均前后快照一致；已有 physical-ghost source 差异保持。
缓存生命周期、复现方式和证据边界见
[变更记录](ai_changes/2026-10-10-cpc-transfer-cache.md)。

## 2026-10-09 索引上下文与头文件整理

现役文件职责与推进调用关系统一见 [README](../README.md#源码职责与数据所有权)。
`31d2967` 提取 OSI Fab 上下文，`7e9bb7c` 将几何构造进一步合入
`OsiIndex.H` 的 `OSI::MakeOsiFabContext(ring, phase)`；当前没有单独的几何工厂。
`8c74b9f` 将共享计时器与 checkpoint 常量原样移入 `AmrCoreLBM.H` 的
`Box3dDetail`，移除 `AmrCoreLBM_detail.H` 及构建登记。

作业 `610666` 的四组日志均记录正常完成 64 步，调度汇总有完整 completed 标记；
输入、app、SHA256、命令与源码差异原位于 `runs/osi_context_matrix_610666/`，
当前工作区未保留该目录；本节作业结果为历史记录，本次未重新验证。
该冻结 app 的 SHA256 为
`2db5e0f089cc2823f10418291cd7c730f3d74e4b59a5055e78a446330ebcea3e`。
direct/fallback 各 1250 条阶段检查、单层周期组 576 条均为 `linf=0`；
direct/fallback 独立 A-B checkpoint 全局差为 `1.165734176e-15`。
非周期 source 诊断仍有 physical-ghost mismatch，valid/internal-ghost mismatch 为零；
周期 source mismatch 为零。不能外推为所有 ghost 来源等价。

头文件合并后 CUDA+MPI 编译链接通过，日志为
`compile-20261009T160419.log`（当前工作树未保存该日志）。
本次文档核对时算例 app 的 SHA256 为
`7516066d02b60d10252ddaab1079751f160bff85b0a136bd9fe9334aa8db1f8e`，
与 `610666` 冻结 app 不同；因此该运行证据属于冻结版本，后续上下文合并与头文件
合并没有新的完整矩阵运行证据。头文件迁移已核对定义原样保留。

## 2026-10-09 固定覆盖掩码与平均后边界修复

提交 `d84eaca` 移除 `lbm.cf_mask_mode` 配置和无掩码分支，覆盖掩码固定启用。
现役边界修复的调用链、covered/uncovered 筛选和 valid/ghost 范围统一见
[架构文档](osi_algorithm_and_architecture.md#101-完整平均后的边界修复)。
此前 `40b6c05` 的粗化求交实现已被掩码筛选取代；早期“单层也修复”说法不再适用。

该轮可执行文件由 `logs/compile/compile-20261009T145941.log` 对应的 CUDA+MPI 构建生成，
SHA256 为 `def82cc37115d4f41b69a44d49a43808473a330a3197091698f38cfab5f8744c`，
当时与作业 `610641` 冻结的 `app.ex` 相同；该轮记录的作业状态为 `SUCCEEDED`。

- 单 rank、双 rank 各 12 组非均匀 DDF 检查覆盖两种布局、非零 phase、单/多层、
  单/多 Fab、贴壁/域内加密和全周期；目标外 valid/ghost 改变量均为零。
- 修改前基线和新版本 direct/fallback 均在 2 ranks/2 GPUs 上运行 64 步，
  step 32/64 动态重网格建立到 level 2；每组 1250 条 staged check 均为 `linf=0`。
- 新版本与修改前 OSI 基线 checkpoint 的三层全部 valid、每层 27 分量逐值比较
  均为零，包含 covered。不能将它外推为独立 A-B 基准、所有 ghost 来源或长程正确性。
- 这也是 2026-10-08 插值门禁加入后的有限窗口推进回归；模板缺口布局的自动补缺、
  长程、多节点、重启、device-direct，以及边界离散精度阶数仍为 pending。

输入、命令、源码差异、可执行文件、checkpoint、日志和 `validation_summary.json`
原位于 `runs/fixed_covered_mask_20261009/job610641/`，当前工作区未保留，无法复核；
现存[变更记录](ai_changes/2026-10-09-fixed-covered-mask.md)描述当时的检查范围。
以下历史作业均按原版本解释，不能用新结果覆盖旧结果。

## 2026-10-08 插值模板覆盖门禁

提交 `19c2650` 增加普通粗到细 ghost 插值的覆盖检查，`cdffef1` 将辅助函数归入
`src/AmrCoreLBM_amr.cpp` 匿名命名空间；不再使用独立的 `InterpolationCoverage.H`。
当时的 `BuildDirectInterpolationCache()`（现已合入 `BuildInterpolationCache()`）在分配 staging 和建立 copy tags 前检查实际模板来源，
覆盖 A-B、OSI direct 和 canonical fallback 共享的缓存。规则与报错后的处理见
[粗细层传输说明](osi_algorithm_and_architecture.md#11-静态-coarse-fine-传输)，验证记录见
[本次变更记录](ai_changes/2026-10-08-interpolation-stencil-coverage.md)。

当日可执行文件在 2026-10-08 19:28:30 完成 CUDA+MPI 构建，对应
`logs/compile/compile-20261008T192703.log`。编译节点 GCC 11.3 下运行
`bash tests/run_interpolation_coverage_test.sh`，13 项几何/返回路径检查和缺口报错负例通过。
这是当时的几何门禁证据。门禁后的 64 步 DDF 动态 AMR 回归已由上节作业 `610641`
补充，长程仍为 pending；更早的作业记录不能作为新增门禁后的运行验收。

## 2026-10-01 `AmrCoreLBM` 编译单元拆分验收

提交 `23b92b7` 将原 `src/AmrCoreLBM.cpp` 按基础生命周期、AMR、OSI、推进和诊断拆为五个
实现文件，并保留同一个 `AmrCoreLBM` 接口和成员状态。CUDA + MPI 编译在
`main3d.gnu.TPROF.MPI.CUDA.ex` 生成成功；`tests/run_osi_index_test.sh` 通过。

作业 `606276` 使用该提交构建的可执行文件运行 A-B、OSI direct、OSI fallback 和全周期四组，
均到达 step 64 并正常结束。direct/fallback 的阶段内 staged check 为 `linf=0`、
`unequal=0`，周期组 source diagnostics 为零。非周期组仍记录 physical-ghost source
mismatch，但 `mismatch_valid=0`。独立 A-B checkpoint 对比中，level 0/1 的全 valid
 范围分别有 `4.259596758e-3` 和 `4.439611252e-3` 的 covered-cell 差异；active
范围最大差为 `1.165734176e-15`，level 2 为 `1.054711873e-15`。这里的 staged
 oracle、active-cell 对比和 covered-cell 对比是三种不同证据，不能合并成“所有 valid
 单元均为零差”。

## 2026-10-01 完整 Q 路径与双 GPU 验收

提交 `c5601f1` 已移除 `osi_sync_batch_components` 及现役源码中的 q 分批循环；插值、平均下传、回退通信和诊断临时场均按完整 Q=27 处理。持久 `osi_sync_buffer` 已不再出现在源码路径中，但完整 Q 临时 `MultiFab` 仍按调用范围短时占用显存，不能把“移除持久缓冲”表述为“所有临时显存为零”。

作业 `606276` 使用提交脚本 `scripts/submit_osi_full_q_matrix.sh` 申请 2 个 MPI rank 和 2 张 GPU，运行 A-B、OSI direct、OSI canonical fallback 与全周期非均匀初值四组；四组均正常结束。direct/fallback 动态 AMR 运行到 step 64 并生成 level 2。其 staged check 均为 `unequal=0`、`linf=0`，但独立 checkpoint 的 covered valid 范围仍有有限差异，见上节；这证明当前测试窗口的阶段内一致性和 active-cell 近机器精度一致，不等于所有 valid/ghost 来源或多节点 device-direct 已完成验收。

本次验证运行的是带有工作树中 `src/main.cpp` 修改的构建；提交脚本保存了输入、源码差异和可执行文件 SHA256。尚未覆盖单 GPU、device-direct MPI、多节点、多层周期动态 AMR、长程严格逐点对照。

## 2026-10-01 OSI 通信接口封装（历史实现）

同层通信由 `FillBoundaryOsi(lev)` 统一选择 raw direct 或 canonical 回退；
重构时的旧/新 OSI 布局迁移由 `ParallelCopyOsi()` 处理。后者复用 AMReX CPC
拓扑，按源和目标各自的 phase 映射数据，rank-local kernel 与 MPI 聚合交换的
共同执行骨架位于 `src/OsiCommunication.H`。粗到细插值和界面平均仍使用各自的
raw/canonical 混合数据路径，未改成 raw-to-raw `ParallelCopyOsi()`。
`CommunicateOsiLevel()` 的 canonical fallback tag 随本次调用的临时 `MultiFab` 构造，
不缓存失效的 `Array4` 地址；缓存只保存可复用的 Box 区域和 direct-copy tag。606276
已覆盖 2-rank/2-GPU、64 步 direct/fallback 动态 AMR，但单 GPU、重启、多节点、device-direct
和更长时间窗口仍为 pending。

依赖缺失 `config/inputs_osi` 的四个历史 stage 提交入口已于 2026-10-19 删除，
BOX3D_OSI_dev 中的对应副本同时退役。后续单 GPU/重启验收需明确输入快照、
运行参数与对应阶段的检查；入口退役不改变上述验证证据及 pending 状态。

## 历史重构迁移记录（截至 2026-09-29）

提交 `af3b5da` 后，OSI 的 `RemakeLevel()` 若发现该层 `BoxArray` 和
`DistributionMapping` 均未变化，会保留原 `osi_state` 和 `osi_phase`。布局变化时，
`ParallelCopyOsi()` 使用 AMReX CPC 的本地与 MPI 重叠区域标签，按旧 phase
读取 raw 值并写入新布局的 phase 0；新增区域仍由粗层插值填充。
该段描述对应当时版本；后续版本已移除现役源码中的持久 `osi_sync_buffer`，当前状态
以本页顶部的完整 Q 路径记录为准。
这版迁移已通过 CUDA + MPI 编译，尚未完成单/多 rank 动态重构逐值验证；
下列历史作业结果对应各自旧版源码，不能用来验收 `af3b5da`。

## 当前数值与长程运行结论（历史结果截至 2026-09-29）

- 提交 `3806c69` 修复 `AverageDownOsiValidLevel()` 的完整 Q 分量解码。此前逐个 q
  解码反复写入 canonical 工作区分量 0，导致平均下传后 covered 粗层数据错误。
- 修复后的锁步 job `604946` 使用 64^3、单 GPU、最高 level 2，在第 32、64、96、128
  步对所有现有层级的 uncovered valid DDF 比较均为 `linf=0`；第 64、96、128 步的
  `AfterRefineMeshAllValid` 在所有层级也均为 `linf=0`。这是该 128 步窗口的逐值证据，
  不能推断 128000 步终态或多 rank 完全等价。
- 更新版 A-B `604593` 和更新版 OSI `604961` 均完成 Re=1000、128000 步；终态
  checkpoint Header 均记录最高 level 2。`604961` 调度状态为 `SUCCEEDED`，40 个性能
  窗口完整，累计 `total=4017.93 s`，末窗口 `MLUPS_total=1230.97`。对应 A-B `604593`
  累计 `total=5487.08 s`，末窗口 `MLUPS_total=900.04`。两次运行的输入快照相同，
  但可执行文件 SHA256 不同，不能据此发布严格配对加速比；还没有 128000 步终态
  DDF 逐点对照。性能细表和版本指纹见 `docs/MLUPS记录.md`。
- 旧更新版 OSI `604594` 只保留 level 0 到终态，尽管作业正常结束，不能用它作
  三层动态 AMR 性能或数值证据。

## 修复前 Stream 源区域筛查（2026-09-28）

作业 `604902` 使用 64^3、`max_level=2`、64 步锁步诊断，已在 Stream 前对
所有层级 uncovered 目标的实际 pull source 分类并逐值比较。第 64 步的两个首发
差异均不是物理边界点，且 `BoundaryUncovered` 后差异保持不变：level 0
`(61,14,47),q=6`，`physical_boundary=0`；level 1
`(7,97,96),q=5`，`physical_boundary=0`。

- level 0 首发 Stream 源统计：`mismatches=174416`，其中
  `mismatch_valid=36100`、`mismatch_ghost=138316`、
  `mismatch_interface=36100`，最大点的源为 `(61,14,48)`。
- level 1 首发 Stream 源统计：`mismatches=219460`，其中
  `mismatch_valid=0`、`mismatch_ghost=219460`、
  `mismatch_physical_ghost=73536`、`mismatch_internal_ghost=145924`；
  level 1 的首发点源为 `(7,97,95)`。
- level 2 在首发时仍为零差；随后其物理边界 ghost 源出现差异。

该批次在平均下传解码修复之前，所见差异不能继续作为现役首差结论。修复后
`604946` 的第 64、96、128 步逐层 uncovered 和重构后全 valid 均为零差。
旧批次原始证据见
`logs/submit/604902-osi-lockstep-alllevels-64.log`，专项脚本为
`scripts/submit_osi_lockstep_alllevels_64.sh`（历史入口；现由
`./scripts/submit_osi_lockstep.sh 64` 承接预设，复现旧实验仍需原代码和输入快照）。

## 现役配置

权威参数在 `config/inputs`：`amr.max_level=2`、`amr.regrid_int=32`、
`amr.max_grid_size=128`、`lbm.stream_mode=1`、`lbm.collide_mode=1`、
`lbm.osi_local_direct=1`、`lbm.osi_parallel_copy=1`、`lbm.osi_mpi_direct=1`、
`lbm.osi_mpi_device_direct=0`、`lbm.osi_mpi_pipeline_chunk_bytes=2097152`、
`performance.report_int=1000`、`max_step=128000`。默认是 OSI 单数组路径；A-B 基准
需显式覆盖 `lbm.stream_mode=0`。这些通信配置已随提交 `1acbea7` 固化为当前
host-staging 工作配置；2 MiB pipeline 只在已测单节点、单 peer 范围内完成验收，
不能外推到多节点或多层动态 AMR。HMPI/UCX 的 device-direct 仍由能力门禁保护，
当前配置不启用它。OSI 地址使用预计算 phase shift；Boundary 保留坐标缓存，碰撞
显式坐标缓存和 branchless 分支均未保留。

主提交入口 `scripts/submit.sh` 以 `config/inputs` 为唯一当前工作配置，并在启动前复制
到 `runs/<timestamp>_job<job-id>/inputs`。程序从该独立目录运行，PlotFile 使用 `plt_`
前缀，checkpoint 使用 `chk` 前缀；同目录还记录命令行覆盖、完整命令、Git commit、
可执行文件 SHA256 和运行日志。当前输出间隔为 `amr.plot_int=3200`、
`checkpoint.chk_int=32000`。专项 `submit_*.sh` 仍采用各自的历史工作目录约定，不属于
主入口的运行快照合同。

## 近期通信修复与验证（2026-09-26）

- 提交 `1acbea7` 恢复了 `osi_local_direct`/`osi_mpi_direct` 的实际分支选择：
  单 rank 或两个 direct 开关均打开时走 raw direct；多 rank 且
  `osi_mpi_direct=0`，或 `osi_local_direct=0` 时走 canonical
  Decode → `FillBoundary` → Encode 回退。direct pack kernel 完成后增加显式
  当前 stream 同步，再提交 stream 1 的 host staging D2H。
- 编译 `GEN_CCDB=0 ./scripts/compile.sh --no-submit` 成功，日志为
  `logs/compile/compile-20260926T095731-summary.log`。
- 作业 `603873` 在 2 ranks、单层固定网格下分别运行 A-B 与 OSI production 1000 步；
  OSI 使用 host-staging、2 MiB pipeline，两个模式均正常结束且无 NaN/SIG。
- 作业 `603875` 使用 direct host-staging lockstep 运行 64 步；Initial、Collision、
  Communication、Stream、Boundary、Swap 全部 `linf=0`，最终 `staged_check=passed`。
- 作业 `603876` 使用 `osi_local_direct=0`、`osi_mpi_direct=0` 验证 canonical
  回退，同样 64 步全部阶段 `linf=0`，最终 `staged_check=passed`。
- 异步 staging 复测尚未提交：当前交互环境的调度客户端无法解析 UID `2542422`，
  没有生成作业日志；源码同步修复已完成，但该运行态结论仍为 pending。

## 历史诊断（截至 2026-09-24）

- 已定位并修正 step 32 新建 level 1 时的 A-B/OSI 初值路径差异：旧 A-B
  `MakeNewLevelFromCoarse()` 调用 `FillCoarsePatch()`（固定
  `cell_cons_interp`、无非平衡缩放），OSI 调用 `InitializeNewLevelFromCoarse()`
  （使用 `lbm.interp_mode` 并缩放）。现让 A-B 复用后者。
  修正前独立作业 `603468`/`603469` 在首次 `FillGhostLevel(1)` 入口
  读取细层 valid 点 `(1,128,253),q=4`，分别为
  `0.072906735842454579`/`0.076610337500803594`；各自插值出口
  值未变，证明该差异来自新层初值。另有 `603461`/`603462` 的
  通信前后逐值导出：同层 ghost 与各自 valid 源值均完全相等，源值
  已有差异，故单 rank 通信写回没有制造这批差异。
  修正后的 `603471`/`603472` 在插值出口所测 66,048 个同层
  源/ghost 单元 × 27 分量最大差降至 `2.7755575615628914e-17`，
  通信后最大差 `1.1102230246251565e-16`；135,200 个插值 ghost
  单元 × 27 分量仍逐值相等。`603473`/`603474` 的首次平均出口
  level 0 uncovered 逐值相等，interface 最大差
  `2.220446049250313e-16`。撤回临时探针、仅保留初始化修正并
  重新编译后，`603478`/`603479` 在 step 33 的已知 uncovered 点
  `(1,111,111),q=18` 均得到 `0.018585093245395951`。
  这些结果覆盖单 rank、当前部分细化的 33 步窗口；未证明多 rank、
  更长时间或全部细层 valid 逐值等价。原始快照、输入与比较脚本保留
  于对应 `runs/` 目录。
- 独立配对作业 `603459`（A-B）/`603460`（OSI）在 step 32 的 level 1
  首个细步 `CommunicateLevel` 返回后、`Stream` 前导出物理域内全部 ghost
  的逻辑 DDF（每个 Fab 单独计数）。两个作业使用相同输入快照和同一临时
  探针可执行文件；坐标、Fab 编号和区域类别逐条一致。由其他同层 valid
  网格覆盖的 66,048 个 ghost 单元 × 27 分量全部不等，最大差
  `0.0034708560941352634`，在 Fab 0、`(0,128,254),q=4`；无非有限值。
  其余粗层插值填充的 135,200 个 ghost 单元 × 27 分量全部相等。
  先前锁步作业 `603446`/`603455` 的 `Stream` 报差点
  `(11,25,224),q=13` 所对应的插值 ghost 输入 `(12,25,223),q=13`
  在这两个独立作业中同为 `0.018555013294641112`。锁步实现仅对
  OSI 状态调用 `FillGhostLevel`，未同步填充 A-B 参考态的插值 ghost，
  因而该锁步 `Stream` 失败不能单独证明独立 A-B/OSI 在该点真实分叉。
  通信后同层 ghost 的差异已确认，但尚需比较通信前 ghost 与有效源单元，
  才能把差异归因于通信写回本身。两份原始导出与比较脚本保留在各自
  `runs/20260924_203627_job603459/`、`runs/20260924_203633_job603460/`；
  临时探针源码及可执行文件已撤回。
- 第 32 步锁步作业 `603455` 在 `Cycle2(0)` 首次
  `FillGhostLevel(1, ..., true)` 返回后、`AdvanceLevel(0)` 前立即检查
  当时存在的 level 0/1 uncovered valid。level 0 的 49,545,216 个
  DDF 值经主机逐值比较全部相等；level 1 没有更细层，全部 valid
  即 uncovered，参考态有限，现有 GPU 阶段比较的 `linf=0`。
  这证明该探针未发现插值返回时的 uncovered 差异；level 1 此处
  尚无独立主机逐值导出，不能将范数零差表述为独立双作业逐值核对。
  随后的锁步 level 1 首个细步在 `Stream` 后报最大差
  `3.8184455009127732e-05`（`(11,25,224),q=13`）；此处 A-B 参考态
  的插值 ghost 未按独立 A-B 路径填充，故不能据此定位真实首差。
  该锁步作业按诊断设计中止，运行日志保留于
  `runs/20260924_202622_job603455/`。
- 当前源码的 A-B 1000 步作业 `603430` 从头运行到 step 1000 并正常结束；
  命令行仅为此次短测覆盖 `lbm.stream_mode=0`、`max_step=1000`、
  `amr.plot_int=250`、`checkpoint.chk_int=1000`，且关闭收敛自动判定。
  `runs/20260924_193813_job603430/` 保留四个时刻（250/500/750/1000）的
  速度、密度、涡量 PlotFile，以及 `chk00001000`、输入快照和运行日志。
  四个时刻均有 level 0–2；逐个读取所有 PlotFile 的 Fab 数据，未发现
  NaN/Inf。step 1000 密度范围为 `0.9944273638004846`–
  `1.0110050298023918`。这只验证该 A-B 短窗口和输出数据有限，
  不构成长程稳定、收敛或与 OSI 逐点等价的结论。
- 修正主机逐点比较器的 D3Q27 速度表并与 `D3Q19.H` 逐项核对后，job
  `603306` 在 step 32 入口、任何完整平均/边界修复/重网格之前，比较
  level 0 全部 56,623,104 个 valid DDF 值：`unequal=0`、`nonfinite=0`。
  此时只有 level 0，因此全部 valid 即全部 uncovered。
- job `603307` 用**同一套** GPU→host 逐 cell、逐 q 比较，在 step 32 的
  `StepEntry`、`AfterRepair`、`AfterRefineMesh` 三个测点均得 level 0
  全 valid `unequal=0`、`linf=0`、`nonfinite=0`。重网格后
  uncovered 的 49,545,216 个 DDF 值也全部相等；其余 covered 值亦相等。
  因此本次初次重网格和边界修复没有引入 level 0 valid 差异。
- 最终编译版本的 job `603308` 在 step 33 入口测得 level 0 uncovered
  49,545,216 个 DDF 值仍全部相等；covered 存储已有 6,134,587 个值不同，
  最大差 `0.29309816067751038`。随后旧阶段比较在 level 0 `Stream`
  报告 uncovered 差异。covered 在推进后可与 oracle 不同，不能把其
  全 valid 差值误作 uncovered 差值；step 32 的 interface 阶段边界见下文。
- 独立 A-B/OSI 作业 `603370`/`603371` 使用相同的 inputs 快照，在 step 33
  level 0 的 `Boundary` 返回后、`Swap` 前读取 uncovered 格点 `(1,111,111)`、q=18：
  A-B 为 `0.018777451872808406`，OSI 为 `0.018585093245395951`，差
  `0.00019235862741245544`。两份日志均报告 `covered=0`，因此此阶段
  uncovered 区域仍存在差异；单点测量不代表该区域的全局最大差。
- 独立 A-B/OSI 作业 `603374`/`603375` 在 step 32 首次重网格后的第一次
  `FillGhostLevel(1, ..., true)` 返回处，导出 `interp_direct_fine_boxes[1]`
  实际插值写入的全部 ghost。两边的四个 work box 元数据一致，135,200 个按
  Fab 计的 ghost 单元、3,650,400 个 DDF 值全部有限且逐值相等。按所属
  Fab 的面、棱、角 ghost 分类，分别有 3,538,944、110,592、864 个 DDF
  值，三类的 `unequal` 均为 0。该测点在父层推进与细层通信之前；不能据此
  推断后续 interface restriction 或第二个细步也一致。
- 独立 A-B/OSI 作业 `603376`/`603377` 在 step 32 两个细步完成、第一次
  `AverageDownInterfaceLevel(0, true)` 写回之后，导出 level 0 全部 valid 与
  相同的 covered/interface 掩码。uncovered 的 49,545,216 个 DDF 值逐值相等；
  interface 是 covered 内层的 39,880 个粗单元，其 1,076,760 个 DDF 值
  全部不等，最大差 `0.2930981606775104`，位置 `(127,2,126),q=0`。
  其余 covered 的 6,001,128 个值中有 5,057,827 个不等。所有类别均无
  非有限值。此测点不能单独判定差异是在平均调用中产生，还是调用前已有。
- 配对入口/出口作业 `603384`（A-B）、`603385`（OSI 回退写回）确认：
  step 32 的 `AverageDownInterfaceLevel(0,true)` 入口处，interface 的
  1,076,760 个值已经全部不等，最大差 `0.004901078005466977`；
  uncovered 的 49,545,216 个值全部相等。出口处 interface 最大差扩大到
  `0.2930981606775104`。本次平均使 A-B 的 442,368 个 interface 值
  发生变化，OSI 回退路径则改变全部 1,076,760 个；两边都没有改写 uncovered
  或其余 covered。配对出口文件与 `603376`/`603377` 的文件逐字节相同。
- 仅将 OSI 的 `osi_parallel_copy` 从 0 改为 1 的 job `603386` 与 `603385`
  在平均入口全 valid 逐值相同；出口仅 interface 有 634,392 个值不同，
  即 23,496 个粗单元乘 27 个分量。direct 写回后 A-B/OSI interface
  最大差仍为 `0.004901078005466977`，回退写回后为
  `0.2930981606775104`。源码中回退路径从稀疏 `interface_result` 拷入
  `transfer_batch`，却按全部 `interface_mask` 写回；这与额外改写的单元数
  一致，是当时回退路径的写回范围错误。调用前已存在的 interface 差异
  不能归因于这次平均。
- 修正后的回退作业 `603393` 使用与 `603385` 相同的 32 步输入覆盖，
  将写回限制到稀疏 `interface_result` 与粗网格的交集。平均入口文件与
  `603385` 逐字节相同；平均出口文件与旧 direct 作业 `603386` 逐字节
  相同。新回退只改写 16,384 个 interface 粗单元 × 27 分量，uncovered
  和其余 covered 均未改写；结果全部有限。与 A-B 作业 `603384` 比较，
  平均后 interface 仍有差异，最大差 `0.004901078005466977`，与 direct
  路径相同。这个测试证明额外写回已消除，不证明平均入口的细层数据或
  多 rank 回退路径逐值等价。
- 配对作业 `603395`（A-B）和 `603396`（OSI）在 step 32 首次
  `AverageDownInterfaceLevel(0,true)` 入口导出 level 1 全部 valid DDF。
  四个 fine Fab 布局一致；参与稀疏平均的底部两层 fine 子单元共
  3,538,944 个 DDF 值，全部有限但逐值不等，最大差
  `0.00014288488183898662`。其余 fine valid 的 53,084,160 个值也
  全部不等，最大差 `0.002619415982687598`。因此平均后的粗层差异
  至少有已分叉的细层输入这一来源；当前数据不能单独判定 OSI 限制公式
  是否还有额外误差。该细层导出是临时诊断，运行数据保留，源码随后撤回。
- 用户明确要求普通时间步对全部 `interface_mask` valid 单元执行平均。
  `BuildAverageCache()` 现将非周期物理边界上的 covered 面也纳入稀疏
  限制缓存，并断言缓存单元数等于 mask 标记数。step 32 作业 `603408`
  （A-B）、`603409`（OSI 回退）、`603410`（OSI direct）均只改写
  39,880 个 interface 粗单元 × 27 分量，其他 valid 未受本次平均改写，
  结果全部有限；两条 OSI 路径的平均出口逐值相同。A-B/OSI interface
  最大差由入口 `0.004901078005466977` 降为出口
  `0.0028039469435821723`，仍非逐值一致。此处旧 OSI 回退原先虽写满
  mask，但超出稀疏结果的 staging 数据无效；不能把那次越界写回当作
  正确的全 interface 平均。step 33 的配对作业 `603411`/`603412`
  在已知 uncovered 采样点 `(1,111,111),q=18` 的 Boundary 后分别得到
  `0.018777451872808406`/`0.018585093245395951`，与扩大平均前相同。
- job `603389` 临时将 interface 纳入锁步 A-B/OSI 阶段比较：step 32
  `AfterRefineMesh` 的 level 0 全 valid 仍为零差；随后 level 0 的
  `Initial`、`Collision`、`Communication` 均为零差，第一次失败发生在
  `Stream` 后（phase=32，最大差 `0.00052852862318335594`，
  `(1,18,112),q=18`）。旧阶段比较跳过全部 covered 单元，因而漏掉了
  interface。A-B 的 `Stream` 跳过全部 covered（包括 interface），OSI
  则递增 phase，故 covered/interface 存储可能分叉；这一阶段定位不等于
  已证明 active 流场错误，也不支持把首差归咎于碰撞。该作业在首次差异处
  按诊断设计中止；插桩随后撤回并重新构建，现役源码不提供
  `verification.osi_ab_check_interface` 开关。证据见
  `logs/submit/603389-out.log` 和 `runs/20260924_165902_job603389/`。
- job `603355` 的新细层对照使用诊断内部构造的参考态，其 level 1 重构后
  `linf=0` 只证明两套初始化计算一致；后续锁步细层参考态产生 NaN，不能将
  该诊断当作独立 A-B 模式的完整步进结果。jobs `603362`、`603364`、
  `603365`、`603368` 的细层锁步数据均受此问题影响。
- jobs `603298`/`603305` 的早期主机逐点结果**无效**：手写速度表的 q=22、q=26
  z 分量有误，修正后才得到上述零差。旧批次范数诊断在相同测点报告的
  `0.07579002442` 也是假差异：逐 q 范数曾给出 `DBL_MAX`，而同分量的
  min/max 均为 0。其归约/比较路径仍待查；现已移除误导性的自动 A-B 范数输出。
- 当前 `CommunicateLevel()` 根据 rank 数和两个通信开关选择 direct 或 canonical
  回退；`CommunicateOsiLevel()` 实现分批 Decode → `FillBoundary` → Encode。
  `AverageDownOsiValidLevel()` 用函数局部 Q 分量 canonical `MultiFab` 完成
  restriction，已不使用共享 `osi_sync_buffer`；这也不等于整个 OSI 生命周期
  完全不使用该缓冲。旧计划文档中的其他 fallback 叙述仅代表其编写时的实现。
- job `603283` 的旧 level 0 全 valid 范数在修复前、修复后、重网格后均报
  `0.07579002442`，与可靠逐点结果冲突，不能作为数值差异证据。step 31
  `Swap` 与 step 32 入口的零差已由 `603306`/`603307` 的主机比较交叉核对。
- job `603276` 在 step 32 重网格后报告 level 0/1 valid 有限；level 1
  `ab_reference=0`，仅能证明 OSI 值有限。step 33 首个已记录的 uncovered
  阶段失败发生在 level 0 `Stream`，位置 `(1,111,111)`、q=18，
  当时报告的 `Linf=0.00016037875800784668`。job `603277` 在 step 32/64/96
  重网格后的各层 valid 均报告有限；这不证明后续长程计算正确。
- 新可执行文件的 128000 步 job `603093` 虽正常结束，后续网格层级没有
  保持三层；用户在 ParaView 中观察到 NaN。其总耗时不得与旧版
  `596890`/`596891` 当作同版本性能对照，NaN 的首次发生步数仍待定位。

下一次诊断仍应在最终无临时探针版本上，逐值比较修正后整个细层 valid
及 `Stream` 出口的 uncovered，再延长步数并补测多 rank 动态 AMR 回退路径。
锁步参考态须先完成与独立 A-B 一致的细层 ghost 插值，才能用其判断
`Stream` 首差。可在相同细层输入上进一步核对缩放结果与粗层写回值。
旧 GPU 范数诊断的计算与归约实现也仍待核对；异步 staging 和 device-direct
运行态验收仍 pending。保留现有日志、PlotFile、checkpoint 和输入快照供复核。

## 历史验证（按原作业版本）

- 完整动态 AMR 性能记录已更新到 job `596888`：Re=3200 连续三段运行累计到
  step 288000，最后 1000 步窗口为 `MLUPS_solv=1128.94`、`MLUPS_total=1088.70`。
  `596154`/`596532`/`596888` 各自是 0--96000、
  96001--192000、和 192001--288000 的连续段，单段总耗时不代表
  288000 步全程总耗时。
- Re=1000 单 GPU、三层动态 AMR 的 128000 步 A-B/OSI 对照已由 jobs
  `596890`/`596891` 完成。全程 `total` 累计为 5273.57/4522.74 s；40 个窗口的
  平均 `MLUPS_total` 为 934.699/1090.556，最后 3200 步窗口为 935.75/1086.32。
  这是同一旧可执行文件下的历史性能对照；未执行终态 DDF 逐点误差比较，
  不作为当前源码性能基线或多层动态网格严格等价证明。

- CUDA+MPI 构建 `compile-20260914T194426.log` 通过。2 MiB 分块流水的全周期
  job `596145` 和六面非周期 job `596150` 均完成 384/384 次六阶段
  `linf=0`。
- 同节点 1000 步 job `596146` 中，2 MiB 流水将 OSI communication 从
  3.848--3.854 s 降至 3.518--3.529 s，total 从 4.381--4.386 s 降至
  4.045--4.049 s；相比同作业 FillBoundary 的 4.616--4.622 s 快约
  12.3%--12.5%。
- chunk 扫描 jobs `596147`/`596148`/`596149` 分别覆盖 1/4/8 MiB；当前
  单 peer、每 rank 约 16 MB payload 下 2 MiB 是已测最佳值。当前配置已采用该值，
  但多 peer、多节点和多层动态 AMR 仍需单独验收。

- `tests/run_osi_index_test.sh` 已修正为当前 `OSI` 命名空间，并通过。
- CUDA+MPI 当前源码构建通过；阶段 oracle 作业 `589641`（A-B/OSI、单层非周期、64
  步）在 Initial、Collision、Communication、Stream、Boundary、Swap 全部保持
  `linf=0`。`589648` 的 branchless 试验也通过同一 oracle，随后已恢复 if/else。
- 当前 if/else 生产运行 `589647`：Collision 17.4206 s、Communication 9.2777 s、
  Boundary 2.4880 s、solver 34.2746 s。branchless `589649` 的对应 solver 为
  34.2732 s，差异约 0.004%，且寄存器数由 34 增至 36，不构成收益。
- Boundary 坐标缓存对照 `589544`→`589593` 将 Boundary 从 2.7042 s 降至 2.4847 s
  （约 8.1%），但 solver/total 基本不变；应视为局部 kernel 收益。
- CUDA+MPI 构建 `compile-20260912T121424.log` 通过。job `591180` 在 2 ranks、2 GPUs、
  单层 8 Fab、全周期条件下完成 64 步，六个阶段全部 `linf=0`。
- 同作业性能对照 `591181` 中，A-B 为 4.981--4.989 s、420--421 MLUPS，OSI MPI
  direct 为 5.505--5.517 s、380--381 MLUPS；OSI 慢约 10.8%。通信计划和 staging
  缓冲缓存化、pack/unpack kernel 融合后，相比 `591174` 的约 31.7% 差距缩小约三分之二。
- 本地 seam copy 移到远端 MPI 投递之后，与 MPI wait 重叠。job `591186` 的双 GPU
  64 步六阶段 A/B 全部 `linf=0`；job `591187` 中 A-B 为 5.027--5.035 s，OSI 为
  5.342--5.354 s，OSI 差距进一步降到约 6.1%--6.5%。
- CUDA-aware device-buffer 路径已实现并通过 CUDA+MPI 构建。当前 HMPI/UCX 没有可用
  CUDA transport；job `591185` 强制 device pointer 后由 UCX `process_vm_readv`
  报 `Bad address` 并终止。job `591188` 不再强制能力标志，程序在通信前由
  `UseGpuAwareMpi()` 门禁安全拒绝启动。
- 改用 CUDA-aware OpenMPI 4.1.5，并补齐配套 UCX 1.12.1 运行库后，job `595584`
  完成双 GPU 64 步六阶段 A-B，384 项全部 `linf=0`，device-buffer 正确性验收通过。
  job `595585` 中 host-overlap solver 为 4.128--4.140 s，device-overlap 为
  45.896--45.908 s；device 路径的 MPI wait 达 44.846--44.860 s。canonical A-B
  在同一 CUDA-aware 栈上也为 45.519--45.535 s，说明退化属于该 MPI transport，
  不能归因于 OSI 地址或 pack/unpack。

## 2026-09-26 结论边界与待办（历史）

同层跨 MPI direct 已在单层、多 Fab、2 ranks/2 GPUs 的全周期和六面非周期条件下通过
逐阶段 A-B；它仍不能证明多层动态 regrid、restart 或运动 IBM 的逐点等价。
2 MiB 分块流水在已测单 peer 配置下已超过 FillBoundary，但多 peer、多节点和
多层 AMR 的消息数、chunk 大小与重叠收益仍待重新验收。CUDA-aware OpenMPI 路径已经
证明数值正确，但当前 transport 的 device MPI wait 比 host staging 路径高一个数量级，
不得作为生产性能路径。后续需要排查 UCX CUDA transport、rendezvous 协议和 GPU Direct
能力；代码仍不得在未确认 MPI 能力时用 `amrex.use_gpu_aware_mpi=1` 绕过门禁。

当前节点没有 `ncu`/`nsys`，因此尚无硬件内存事务、occupancy 和分支效率计数器。历史
日志、可执行文件和 checkpoint 均保留，未执行清理；当前算例根目录未发现
`Backtrace.0/1` 实体文件，IDE 标签页可能是已删除文件的缓存。

主提交入口现通过 `RESTART_CHECKPOINT` 接收只读源 checkpoint，并在新运行目录创建
本地 `chk<step>` 符号链接；程序后续仍以本地 `chk` 前缀写出，因此不会回写源目录。

## 入口

先读本文件和根目录 `README.md`，再读 `docs/osi_algorithm_and_architecture.md`、
`src/main.cpp`、`src/AmrCoreLBM.H/.cpp` 与 `config/inputs`。性能数字必须同时注明
源码、输入、可执行文件、GPU 和日志；历史 job 不自动等于当前基线。

## OSI ParallelCopy 阶段性状态（2026-09-21）

候选提交 `21a1bab` 已将单 rank 插值和平均接口的 OSI raw direct copy 接入，并把
Q=27 分量合并为单次 kernel。真实 GPU 作业 `601985`（A-B）与 `601991`（OSI direct）
在相同 128^3、36 步、部分细化输入下，step 32 的插值/平均分别为
`0.267/0.266 ms` 与 `0.314/0.274 ms`；step 36 分别为 `0.727/0.800 ms` 与
`0.706/0.739 ms`。这只支持当前单 GPU 短窗口的阶段性性能结论。

该日期的配置快照将 `lbm.osi_parallel_copy` 设为关闭；当前工作树已设为 1。
跨 rank 平均 host-staging direct 已接入，
下面的逐 q 与多 GPU 短窗口证据只覆盖固定网格、重启后 8 步的 active cells；
动态 regrid、多节点及 device-direct 平均的运行验收仍为 pending。详细阶段设计
见 `docs/osi_parallelcopy_optimization_plan.md`。

### 2026-09-21 新增多 GPU 证据

- 作业 `602046`：2 ranks/2 GPUs、六面非周期、64 步，A-B oracle 共 384 次阶段检查，
  全部 `linf=0`；逐阶段记录在
  `logs/validation/osi_ab_stage_602046.jsonl`。
- 作业 `602047`：2 ranks/2 GPUs、8 Fab、1000 步同层 A-B/OSI MPI direct 对照；OSI
  communication 约 `4.21--4.23 s`，A-B 约 `4.03--4.06 s`。
- 作业 `602049`：2 ranks/2 GPUs、三层 AMR、1000 步 A-B/OSI 对照；OSI 的
  `interp/average` 约 `3.70/2.28 s`，A-B 约 `2.54/1.25 s`。此作业中
  `osi_parallel_copy=0`，因此它验证的是当前跨 rank 回退链，不是平均 raw direct。
  分项记录在 `logs/validation/mpi_parallelcopy_perf_602047_602049.jsonl`。

### 2026-09-22：平均 direct 路径及运行边界

#### `osi_sync_buffer` 在 2026-09-22 的使用边界（历史）

以下是当时的代码状态；现役函数行为以本文件顶部 2026-09-24 诊断为准。
当时代码仍保留 `osi_sync_buffer`，但它不再是所有 OSI 通信阶段的必经中转：

- 同 rank ghost copy、OSI 插值 direct 路径，以及已启用的同层 MPI raw pack/unpack
  不需要先把整层 raw state 解码到该批次缓冲；
- `CommunicateOsiLevel()` 仍把它作为 canonical fallback，供
  `osi_local_direct=0` 或 direct 路径不可用时使用；
- 当日的 `AverageDownOsiValidLevel()` 仍在 restriction 前后将 fine/coarse valid DDF
  分批解码到 `osi_sync_buffer`，再调用 AMReX `average_down`；这只是 2026-09-22
  的历史实现状态。
- regrid、checkpoint/state 重建和诊断中的批次缓冲使用属于独立生命周期，不能据此
  宣称生产 OSI 路径已经彻底移除 `osi_sync_buffer`。

该历史待办已由提交 `3806c69` 完成：现役 restriction 使用函数局部 Q 分量
canonical `MultiFab`，不再通过共享 `osi_sync_buffer` 中转。后续仍需把通信、重构、
checkpoint/state 重建和诊断中的独立缓冲分别按当前调用路径验收，不能把 restriction
修复扩大解释为整个 OSI 生命周期完全不使用任何批次缓冲。

- 提交 `f3ec9b7` 的作业 `602116`：2 ranks/2 GPUs、level 0/1、固定布局，
  从共同 step 32 checkpoint 续跑至 step 40；OSI host-staging direct 平均执行完成。
  与 A-B 的 canonical checkpoint 对比，54 个 `(level,q)` 的 active-cell
  `Linf=0`，global `Linf=0`；level 0 被细层覆盖的 valid storage 有非零差异，
  不能称为所有 valid cells 位相同。证据：
  `logs/validation/osi_parallelcopy_checkpoint_602116.jsonl` 和
  `logs/submit/602116-osi-parallelcopy-check.log`。
- 同作业每 rank 的 8 步短窗口均值：A-B/OSI 插值分别约 15.238/16.238 ms，
  平均分别约 5.604/5.528 ms；这是固定布局短测，并非长期动态 AMR 性能承诺。
  分项见 `logs/validation/osi_parallelcopy_perf_602116.jsonl`。
- 提交 `bc26475` 实现平均阶段 GPU-aware device-direct 分支并通过 CUDA/MPI 编译。
  提交 `9cac08a` 的作业 `602119` 在 HMPI/UCX 普通 OSI MPI 通信阶段报
  `process_vm_readv: Bad address` / `MPI_ERR_INTERN`，未到达平均、也无逐 q
  比较结果，不能宣称该分支运行正确。专项脚本现默认复现 host-staging；只有显式
  设置 `OSI_AVERAGE_MPI_TRANSPORT=device-direct` 才会启用 device pointer 测试。
