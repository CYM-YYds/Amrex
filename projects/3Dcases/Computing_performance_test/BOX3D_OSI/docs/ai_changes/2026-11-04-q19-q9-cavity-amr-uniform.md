# Re=1000：D3Q19 / D2Q9 方腔 AMR 与均匀网格对照

目的：检验同等最细分辨率下，AMR是否能在保留速度场精度的同时降低到达相同物理时间的推进成本。先做t=25短测筛选，通过后续跑t=1000；不预设AMR必然优胜。

D2Q9只能描述二维流动，本轮按二维顶盖方腔处理；D3Q19为三维顶盖方腔。正式生产源码、config/inputs及用户后处理修改均未改变。

## 冻结与实验入口

新临时入口 [experiment.py](../../scripts/temporary/cavity_models/experiment.py) 与 [submit.sh](../../scripts/temporary/cavity_models/submit.sh) 管理两个模型、同物理时间配对、短测门槛及后续长测。原a5_threshold_probe入口固定三维和128基础网格，uniform512_local_staging入口绑定Q27历史程序且导入即运行，均不能直接承担本轮双维数/双模型对照。沿用guard_run.py、既有MPI启动约定、VisMF头解析/memmap方式和本地落盘后8MiB分块/fsync/SHA256回读发布协议；没有扩大正式配置接口或引入新的求解路径。

冻结目录：[round1](../../runs/temporary/cavity_models/round1/)。包含设置、原始输入、完整模型源码、构建日志、程序、SHA256和工作树diff。程序复用前一轮job626009中从当前源码编译并通过逐值检查的模型程序：D3Q19选择128线程，D2Q9选择256线程，AMR/均匀使用同模型同一程序、同一线程配置。只通过已有实验分派选择编译期启动实例，不修改碰撞公式；避免无源码变化时重复编译。前轮源码与正式当前源的差异仅为测试启动分派及三维Q选择，详见[线程验证](2026-11-04-q19-q9-collision-threads.md)。

|参数|D3Q19|D2Q9|
|---|---|---|
|维数、Re、L、顶盖速度|3D、1000、1、0.05|2D、1000、1、0.05|
|精度、存储、碰撞|FLOAT、OSI、BGK|FLOAT、OSI、BGK|
|AMR基础网格 / 最高level|128³ / 2|512² / 2|
|均匀网格 / 最细等效分辨率|512³|2048²|
|lbm.err|0.25 / 0.5 / 1 / 2（D3Q27 C组）|0.64 / 1.28 / 2.56 / 5.12|
|max_grid_size逐层|128 / 256 / 512|512 / 1024 / 2048|
|blocking_factor、grid_eff|16、0.7|16、0.7|
|重网格间隔、插值|32粗步、interp_mode=0|32粗步、interp_mode=0|
|碰撞线程/block（均匀与AMR一致）|128|256|

D3Q19采用最新D3Q27 C组job624717的物理/AMR参数。每t=25输出速度及Q/涡量；关闭OSI嵌套细分计时，启用重网格DDF有限值与最终有限值检查。均匀网格使用nomask/reduced-sync已有快速路径；AMR仍使用masked碰撞。均匀网格每单位物理时间的步数为AMR粗层的4倍，以相同终点时间比较，不能拿相同步数或MLUPS直接代表AMR收益。

## t=25短测：job626026

同节点whshare-agent-96、同一张A100-PCIE-40GB、单MPI rank，串行执行同模型AMR/均匀配对。四次应用退出0，PlotFile/checkpoint均已转存并逐文件SHA256回读验证；checkpoint核对模型、终步、物理时间。

|模型|AMR / 均匀步数|AMR推进秒|均匀推进秒|均匀/AMR耗时比|AMR valid-cell减少|采样速度RMS / U_lid|最大差异 / U_lid|
|---|---:|---:|---:|---:|---:|---:|---:|
|D3Q19|3200 / 12800|34.5761|284.2770|8.2218|88.5132%|0.18797%|1.47186%|
|D2Q9|12800 / 51200|8.7502|14.4170|1.6476|91.1499%|0.06279%|0.51923%|

推进成本取五个完整窗口的perf(s).total累计，包括重网格，不包括初始化、输出和共享盘传输。对应AMR/均匀的应用墙钟（含初始化和本地输出、不含共享盘转存）分别为37.626/623.439秒、11.031/16.638秒；它不是表中推进计时，尤其Q19均匀场输出开销不可忽略。本次只有一条短测轨迹配对，不是重复统计的稳定加速比，早期瞬态网格不能替代t=1000结果。

误差以均匀网格为数值对照，采用每方向33个固定最细格中心采样点，逐层覆盖取最细可用leaf cell值。三维35937点、二维1089点，速度各分量差以U_lid=0.05归一化。该指标是抽样差异，不是全场积分范数或外部稳态参考误差。另保存沿几何中心竖向的格中心剖面，未冒充ParaView连续插值中心线。网格统计包括被细层覆盖的粗层valid cells、不含ghost；不能据其声称显存节省比例。

短测门槛预设为：有限值/重网格检查通过、推进耗时至少少约10%（uniform/AMR>1.1）、采样速度RMS/U<0.02且最大差异/U<0.15。它仅用于进入长测，不能当作数值方法精度验收标准。两组均通过。

短测报告与图：

- [D3Q19报告](../../runs/temporary/cavity_models/round1/job626026/D3Q19_report.md)、[图](../../runs/temporary/cavity_models/round1/job626026/D3Q19_comparison.png)。
- [D2Q9报告](../../runs/temporary/cavity_models/round1/job626026/D2Q9_report.md)、[图](../../runs/temporary/cavity_models/round1/job626026/D2Q9_comparison.png)。
- 同目录comparison.json、profile.csv、status.json、每次command.json/overrides.txt、完整日志、场、checkpoint及transfer_manifest.json。

## t=1000长测启动

在短测门槛通过后，用户已授权的长测分别提交为：

- D3Q19：[job626033](../../runs/cavity_models_t1000/job626033/)，从两组各自t=25 checkpoint继续。AMR按250/500/750/1000分段，checkpoint间隔t=250，保持C组输出约定；均匀网格受本地盘容量限制按t=25分段并保留checkpoint，沿用既有Q27均匀512³的恢复/转存策略。
- D2Q9：[job626034](../../runs/cavity_models_t1000/job626034/)，两组各从t=25连续到t=1000，checkpoint间隔t=250。

每个模型的两条长轨迹仍在同一作业/节点/GPU上串行执行；两模型分属不同作业，不能作跨模型严格性能归因。正式目录purpose.json记录长期验证属性和保留范围。运行时进展见progress.json，分段结果见status.json；遇运行或转存失败会退出并保留本地及.partial证据。全部成功后自动合并短测与续跑窗口、输出t=1000采样差异/剖面、耗时报告和图，最后才写completed.txt。

启动提交时，两条长测均已启动、从checkpoint恢复并正在推进，尚未完成。不能把短测8.22/1.65倍写成长测结论；最终还需核对完成标记、所有分段退出、场/checkpoint的Header和校验记录、t=1000对照及收敛日志。重启会重置收敛快照/连续次数；完成t=1000或中心线接近不代表正式converged=1，未做Ghia/Cortes外部稳态精度验收。

## 验证、排除与保留

Shell/Python语法检查通过；本地VisMF采样器已在保留的三层Q27场与本轮Q19/Q9场上检查维数、组分、覆盖和有限值。原始程序来自前轮两维构建/12组逐值对照，当前无求解源码修改。文档链接、路径、图渲染与diff检查通过。

job626022、626025在计算前被本地盘容量检查拦下，不进入任何统计。前者按长批次预留55GiB，后者预留18GiB高于节点实有约16GiB；按本轮Q19单批约12GiB输出及余量改为14GiB检查后，job626026成功。历史及失败日志保留，不清理其他任务的本地数据。

本轮保留输入、源码、程序、命令、所有日志/状态、报告/图、场和checkpoint。短测属于temporary，t=1000续跑属于正式独立验证；不迁移或删除历史数据。暂未测设备显存峰值，未给出显存优势结论。

## 2026-11-05：t=1000完成与报告恢复

四条计算轨迹均已到t=1000。job626033的43个续跑应用（AMR 4段、均匀39段）及job626034的2个续跑应用全部退出0，输出全部完成SHA256转存校验。两作业原始调度状态为FAILED，原因发生在所有计算/转存完成后的报告阶段：`glob('*_t*')`把`*_t250-guard.log`等日志也当作分段目录并解析整数。已仅筛选具有纯数字时间后缀的目录，并扩展analyze入口直接从已有结果恢复长测汇总，无需重算。

原调度失败状态、日志及failure.json保留；postprocess_recovery.json记录修复方式，completed.txt表示计算、转存和补生成报告已完成，不改写调度器历史。恢复时复核全部批次退出码、转存manifest文件尺寸与程序finalized标记；未重复读回约数百GB做第二遍SHA256，原转存时的逐文件SHA256回读证据完整保留。

|模型|全程AMR推进秒|全程均匀推进秒|均匀/AMR耗时比|终态valid-cell减少|终态采样RMS/U|最大差异/U|
|---|---:|---:|---:|---:|---:|---:|
|D3Q19|2862.4322|11519.5662|4.0244|87.1765%|0.31119%|3.02661%|
|D2Q9|1022.2124|1099.4308|1.0755|91.2048%|0.14094%|0.39238%|

全程耗时包含短测t=0--25及续跑t=25--1000；共44个统计窗口/轨迹，步数逐窗求和核对。初始短测来自agent-96，两条续跑作业均在agent-61，但GPU PCI地址分别为01:00.0和02:00.0，使用不同GPU；同模型每个阶段AMR/均匀同节点同GPU串行，初段/续跑节点不作同环境混同。统计包括重网格，不含初始化、场/checkpoint输出和转存；不同恢复次数与冷启动影响不能据此排除，单次轨迹不是重复性能置信区间。

D3Q19的AMR推进耗时约为均匀网格24.85%，本轮优势明显；D2Q9仅少约7.02%，显著小于短测，不能继续引用短测1.65倍或宣称二维同样具有大幅稳定收益。采样误差仍按本记录前述固定leaf cell采样定义，不是外部参考精度验收。未测显存峰值。

流场保留审计completion_audit.json已核对：每种模型、每种网格各40份速度场和40份Q/涡量场，时间均为25、50、…、1000，终态checkpoint步数/时间均匹配；完整序列分布在短测t=25与各长测分段目录，不只保留终态。四条轨迹末次正式收敛标记均为converged=0，运行完成不能表述为达到稳态收敛。

最终报告与图：

- [D3Q19报告](../../runs/cavity_models_t1000/job626033/D3Q19_report.md)、[图](../../runs/cavity_models_t1000/job626033/D3Q19_comparison.png)、同目录D3Q19_comparison.json/CSV及completion_audit.json。
- [D2Q9报告](../../runs/cavity_models_t1000/job626034/D2Q9_report.md)、[图](../../runs/cavity_models_t1000/job626034/D2Q9_comparison.png)、同目录D2Q9_comparison.json/CSV及completion_audit.json。

恢复命令为仓库根`.venv/bin/python <case>/scripts/temporary/cavity_models/experiment.py analyze <long-job-directory> <short-job-directory>`。Python语法、真实长测全量窗口汇总、场时间序列/终态Header、报告渲染、文档链接与diff检查通过。
