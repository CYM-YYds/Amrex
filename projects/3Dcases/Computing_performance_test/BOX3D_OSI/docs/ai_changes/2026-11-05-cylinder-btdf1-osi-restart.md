# 二维圆柱 BTDF1 → OSI：520000 步续算与颗粒计时

## 目的与范围

按用户要求，将 `Cylinder2D_BTDF1` 的绕流物理与 IBM 算法迁入 BOX3D_OSI，
从原始 `chk00520000` 继续 64000 个粗层步，目标 584000，并保留流场和颗粒分项耗时。
基线提交为 `a56d873fefd4045f80dd9287b510197164f1f3e5`。
本次使用独立实验源码副本，未修改正式 `src/`、默认 inputs 或原始圆柱检查点。

实验入口见 [复现说明](../../scripts/temporary/cylinder_btdf1_restart/README.md)。
完整实验位于 [cylinder_btdf1_restart_520000](../../runs/cylinder_btdf1_restart_520000/README.md)。
脚本及输入随本记录提交；实验中保留源码、补丁、程序、SHA256、命令、日志、检查点和流场。

## 模型与迁移契约

- 二维 D2Q9，场和粒子均为双精度；压力型 He–Luo，`p0=1`、`rho0=3`、`U0=0.03`。
- 基础网格 800²，物理域 1²，圆心 (0.5,0.5)，半径 0.0125，Re=40 按直径 0.025 计算。
  最高 L3，相邻层细化比 2，最细圆柱直径 160 格。
  实际黏度为 1.875e-5、粗层 tau=0.545；沿用的日志 PrintLbmParm 按域长打印 Re=1600，
  圆柱直径参考 Re 仍为 40，见实验 physical_parameters.json。
- 静止圆柱、NF=1；原平滑三点核、厚度系数 1.9、502 个标记、单位厚度力积分。
  粒子静止几何可解析重建；导入后不复用旧粒子容器的字段布局。
- 保留入口给定速度和其余三面外推，以及原角点覆盖顺序。
  原算法在步入口计算新边界，实验通过暂存边界值保持该时间语义，覆盖域内物理面的 valid 和 patch ghost。
- 加密使用原圆柱外侧 10 个当前层网格的距离带，以及 L0/L1/L2 的涡量阈值
  0.1/0.4/[0.6,4.0]；L2 限制在圆心上下 1.5D。每 10 步重网格，error buffer=0。
- 原 checkpoint 的 valid DDF 保持数值，仅按明确的 D2Q9 方向表重排；不缩放压力分布。
  只有独立导入副本带 `ImportBTDF1` 标识，原检查点哈希复核未变。
- 原程序的参考单卡续算使用另一检查点副本，只将 Header 中四层 DistributionMapping 改为 rank 0；
  DDF 与粒子二进制不变。旧程序直接读取四 rank 归属会报 `MPI_ERR_RANK`，该失败记录保留。

AMR 推进、覆盖掩码、插值和平均使用 BOX3D_OSI 框架；这不是整个原算例的逐值移植。
本实验为保留边界时间语义增加了暂存缓冲及分项同步，不能把其绝对性能当作默认方腔程序的性能。

## 验证

最终程序及源码的 SHA256 位于 [accepted_manifest.json](../../runs/cylinder_btdf1_restart_520000/accepted_manifest.json)。
完整结果见 [validation_passed.json](../../runs/cylinder_btdf1_restart_520000/validation_passed.json)：

- 最终二维 CUDA/MPI 构建通过；接受短测作业 626334、重启诊断作业 626335 正常结束。
- 原始 520000 步 DDF 直接恢复的速度，与导入后的初始流场逐 valid cell 对照，最大差 1.0408341e-17。
- 64 个粗层步、每 10 步动态重网格：L0–L3 全 valid DDF（含 covered）A–B/OSI 最大差 5.5511151e-16。
- 520032→520064 原生重启与连续运行的全 valid DDF 最大差为 0。
- `prepare.py` 可逐字节重建实验源码及构建配置；Python AST、Shell 语法、原检查点哈希复核通过。

早期物理面只更新 valid、漏掉 patch ghost，首步 L1 出现 6 个非有限 DDF；
补齐与原 Boundary 相同的 grown-Fab 域内物理面范围后消失。
早期初始流场尚未从导入 DDF 完整重建，产生零初始场和中途重启小差异；接受版显式重建初始宏观场，重启对照为 0。
这些失败轮与程序副本保留，不作为接受结果。诊断采用仓库 [阶段范围规则](../../../../../../.agents/skills/stage-aware-amr-diagnostics/SKILL.md)。

独立原程序参考作业 626328：在 520064 步，原程序 Cd≈1.51302，迁移版≈1.51269。
两者 AMR 拓扑不同；最细层共有 630784 个 valid cell，交集为全部最细层，
速度分量最大差约 4.1373e-6、DDF 最大差约 1.4106e-6。
见 [donor_comparison.json](../../runs/cylinder_btdf1_restart_520000/donor_comparison.json)。
这只是短程迁移参考，不构成长期精度、收敛或网格无关性证明。

## 正式任务与统计

已提交单 GPU、单 MPI rank 作业 **626343**，从原始 520000 步导入至目标 584000；
本记录提交时任务正在推进，尚未完成，不能将短测结果当作 64000 步完成结果。
运行目录为 [production](../../runs/cylinder_btdf1_restart_520000/production/)。

- 保留 520000 步初始速度场；每 4000 步输出速度与涡量/Q 场；每 16000 步保留 checkpoint，不删除旧文件。
- 每 1000 步输出窗口累计耗时、网格规模和 MLUPS；每次流场输出及最终状态检查全 valid DDF/宏观量有限性。
- `particle_total` 包含宏观量准备、插值/施力、SumBoundary 与 force ghost 四个子项，不能再次累加子项。
- `particle_reduce` 包含受力归约与 CdCl 文件输出；`particle_redistribute` 为重网格后的颗粒迁移。
- `total/solv` 已包含颗粒工作；`total` 不含场与 checkpoint I/O，程序 Total Time 和 resource_usage.txt 给出外层耗时。
- 长任务退出时自动生成 `production/timing_windows.csv`、`summary.json`、`summary.md`。
  完成标志需要终点步、程序正常退出及完成记录，未完成时报告保留 `complete=false`。

未自动停止于旧 Cd 收敛阈值，以完成用户要求的固定 64000 步。未提交仓库中其他并行任务的文件。

首个正式 1000 步窗口：计算 total=125.4394 s，颗粒相关路径=57.6540 s（约45.96%）；
其中 particle_total=55.6117 s，SumBoundary 子项=31.1187 s。仅首窗观测，非稳定性能结论。
checkpoint 间隔沿用绝对步数整除规则，本次常规写出步为528000/544000/560000/576000；终点584000保留速度/涡量场及全 valid DDF 的应用内有限性检查。
