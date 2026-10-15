# BOX3D_OSI_dev 二三维通用化

## 目的与范围

在开发副本中支持同一套源码构建 DIM=2/D2Q9 与 DIM=3/D3Q27，默认三维。
原 BOX3D_OSI 源码和数值路径未修改；公共构建、MPI 启动脚本的可选参数见
[仓库级记录](../../../../../../docs/ai_changes/2026-10-15-dimension-build-entrypoints.md)。

## 主要改动

- 格子定义改名 LbmLattice.H，由 AMREX_SPACEDIM 决定维度和 Q；保留三维默认排列。
- 初始化、BGK、迁移、宏观量、边界、涡量、OSI 几何和 MPI 索引按有效维度访问。
  二维 z=0、环长度=1；顶盖为 y 高端，三维仍为 z 高端，角点顶盖优先。
- 三种自定义 DDF 插值、ratio=2 平均、覆盖掩码与 regrid 支持二维；新层仍只初始化 valid。
- 三维粒子、IBM、SGS/cumulant 实现通过编译条件隔离；二维不支持粒子 checkpoint。
- 新增 inputs.2d，DIM=2 编译/提交入口、二维速度输出、checkpoint 模型元数据与读取前兼容检查。
- 锁步 Remake 同步重建 A-B 参考态布局，复用 canonical 重构路径；比较器增加布局断言。
  原路径在受控布局变化后访问旧参考态，预跑 613517 于第 64 步触发 CUDA 非法访问。
- checkpoint 逐值诊断统一复用原 OSI 分支的 pinned 掩码回拷及 valid/active 分类。
  原 canonical 分支在主机读取设备掩码，首次套件中的四项比较以信号 11 失败。
  本修正不改变推进公式，也不将 covered 状态差异算作有效解误差。

## 验证与证据

基线为修改前 afca3b0 的三维源代码、配置及独立编译程序，保存在
[基线目录](../../runs/dimension_port_20261015/baseline/)。二三维 CUDA+MPI 编译及维度切换通过。
局部生产内核检查在两种维度均通过 ASan/UBSan，覆盖格子矩、三个插值模式、平均、
OSI 地址双射、两种 BGK、宏观量、顶盖角点和涡量；原独立 OSI 地址测试通过。
当前编译数据库覆盖全部九个三维算例源文件，所有条目使用 DIM=3；无改动重编复用数据库。
编译和局部测试日志见 [build_evidence](../../runs/dimension_port_20261015/build_evidence/)。

| 验证 | 结果与边界 |
| --- | --- |
| 作业 613518 二维矩阵 | 48/48 正常结束；64² 基础网格、最高层 0/1/2/3、插值 0/1/2、1/2 rank、方腔/周期；每组 256 coarse steps，regrid_int=32 |
| 阶段逐值对照 | 371351 条 osi_ab_stage 记录，最大 linf=0，无非有限阶段记录；另启用逐 Stream source 检查及末态 active 有限值检查 |
| 实际 AMR 生命周期 | 受控夹具记录新建层 144 次、Remake 108 次、清层 72 次；最高层创建也逐用例核对 |
| 二维输出与生产模式 | PlotFile 为二维 ux/uy；独立 A-B/OSI 生产模式对照各层 active_linf=0 |
| 正式提交入口 613523 | DIM=2 经算例 submit.sh 与公共 MPI 入口运行成功；manifest 选择 inputs.2d/main2d，单 rank 完成 2 步且末态有限 |
| checkpoint 兼容 | 新旧三维 checkpoint 均被二维入口正确拒绝；旧三维 checkpoint 可由三维重启 |
| 初次完整套件 | 62/66 通过，四个 canonical 对照因上述主机掩码访问问题崩溃；保留原失败记录 |
| 补测作业 613522 | 6/6 通过：二维 A-B/OSI 128→256 重启、三种插值的三维 A-B 64 步旧版本回归，以及三维 OSI 回归；各层 active_linf=0 |

结合首次套件与补测，66 个不同验收场景均有通过证据；不是声称 613518 作业整体成功。
[初次报告](../../runs/dimensions_613518/summary.json)、
[矩阵指标](../../runs/dimensions_613518/matrix_metrics.json)、
[补测报告](../../runs/dimensions_recheck_613522/summary.json)、
[合并验收记录](../../runs/dimensions_recheck_613522/acceptance.json)
包含具体用例。两次作业均冻结程序、源码、输入、命令、hostfile 和 SHA256；当前可执行文件
与成功补测的程序 SHA256 一致。完整复现入口为 tests/submit_dimension_validation.sh；
补测脚本保存在 runs/dimensions_recheck_613522/recheck.sh。

## 数值结论限制

以上误差结论针对对应阶段的检查区域以及 checkpoint 对照的 uncovered active DDF。
全 valid 范围并非全部相等：二维重启的 covered 区域最大差异约 5.8950e-4，
三维新旧对照 covered 区域最大差异约 4.5070e-3；对应各层 active_linf 均为 0。
保存 checkpoint 与末态检查属于不同阶段；本次不将 covered 状态差异视作有效解误差，
也未对其逐项归因。原三维四层历史验证警告仍保留，不据此推定新的长期验证结果。

未进行长期方腔收敛、文献曲线对比、性能优化或二维 GPU-aware MPI 验收。
本次 MPI 运行使用 host staging；受控 regrid 夹具默认关闭，不改变生产加密判据。
