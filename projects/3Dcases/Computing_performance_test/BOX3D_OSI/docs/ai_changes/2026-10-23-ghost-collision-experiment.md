# 同层 ghost 碰撞裁剪实验

目的：检验跳过同层通信可覆盖 ghost 的碰撞是否正确、是否改善真实 AMR 布局的吞吐率。正式 src/config 未修改，不启用该优化。

## 实现与归属

复用 iMultiFab::FillBoundary 标识同层 valid（含周期映射）可覆盖区域；候选只在 OSI 碰撞中跳过 valid 外、同层可覆盖的 ghost，保留粗层插值 ghost 和原 covered/interface 规则。掩码由求解器拥有，在 Collide 按 BoxArray/DistributionMapping 懒重建；不缓存 DDF 或 phase。新增缓存是为了避免每步重新搜索邻块。候选仍发射原矩形区域，因此新增分支/掩码开销，未消除无效线程发射。

实验目录：[ghost_collision_20261023](../../runs/ghost_collision_20261023/)。其中 source.diff/git_head.txt 是实验前现有工作区溯源；candidate/src 是当时完整源码快照，candidate.patch 是相对于该快照的候选改动。包含用户已有修改，但未将其纳入本次提交，也未覆盖正式源码或可执行文件。

复现入口：[测试脚本](../../scripts/temporary/test_ghost_collision.sh)、[审计脚本](../../scripts/temporary/analyze_ghost_collision.py)、[候选补丁](../../scripts/temporary/ghost_collision_candidate.patch)。补丁用 `git apply --unidiff-zero` 应用到实验前源码快照（不是任意历史提交），用根 scripts/compile.sh 在隔离目录编译。GHOST_TEST_MODE 支持 all/control/timing；调度包装器放实验目录。脚本和补丁是本次实验材料，结果需要复现时保留，不是正式生产入口。

## 正确性

- 两次候选编译均成功；最终二进制由完整构建后增量重建获得，各 job 目录记录 SHA256。
- job617898：3D D3Q27，基础 32³，最高 level 2，max_grid_size=16，BF8（用于多块/多rank阶段验证，不用于下方性能计时）；三种插值模式 × 非周期/全周期 × 1/2 ranks，共12组，每组64 coarse steps、regrid_int=16，使用已有 regrid_fixture 和 A-B 阶段检查。
- 日志审计：19860 条阶段检查的最大 linf=0；2376 条迁移源检查中的内部 ghost mismatch 全部为0。统计条数不等于格点数，也不表示每个阶段都检查所有 covered 单元。
- 双 rank 非周期的物理域外 source 有 A-B 差异；job617899 关闭优化后三种插值模式的物理 mismatch 计数均与候选相同（各2453208），内部 ghost mismatch=0、阶段最大 linf=0。该物理 ghost 问题独立保留，不能声称所有 source 完全一致。
- 同程序开关对照（job617898/reference 与 candidate）：相同初值、三层 fixture、64步，checkpoint DDF 比较 global linf/l1/l2 均为0。逐层 valid/active 结果见原始日志；不以进程退出码替代逐值证据。

## 性能与边界

job617898 首次真实 checkpoint 动态重网格计时在关闭优化时 OOM（exit6），排除性能统计；当前加密判据与615364历史版本不同，不宣称重现其动态性能。job617897 因提交入口未加执行位未启动，修正后重提。

job617900 在 whshare-agent-66、A100-PCIE-40GB 上，单 rank 使用 GPU0（申请两卡以复用验证资源配置）。同一个程序、同一615364 step256000 checkpoint，每次运行到256320；固定网格 regrid_int=-1，BF32，关闭输出、checkpoint、收敛和 OSI 细分计时。按 off/on/on/off 顺序，两次/模式，每次两个160步窗口；四次应用均正常结束。范围为固定真实布局，不能外推动态重网格或历史615364全程。

| 每160步窗口，四窗口均值 | 关闭 | 开启 | 变化 |
|---|---:|---:|---:|
| perf total (s) | 9.684633 | 9.667586 | -0.176% |
| collide (s) | 6.841140 | 6.660155 | -2.647% |
| MLUPS_total | 876.151 | 877.689 | +0.176% |

窗口 total 范围：关闭9.610808–9.735613 s；开启9.619181–9.721924 s。差异重叠，未发现稳定端到端收益；短窗口碰撞分项也有明显波动，不据此声称稳定内核加速。分层调用次数、launch_cells 应相同（仍发射原区域），审计文件保存完整分层日志。launch_cells 不等于实际执行碰撞的单元数。

结论：这些测试支持在所测条件下省略同层 ghost 重复碰撞，但当前掩码候选没有足够性能证据支持合入。保留实验，生产不改。若继续，应衡量可裁剪格点比例，并比较区域发射方案的额外启动成本。

## 2026-10-26 真实网格长窗口复测（已完成）

617898的32³基础、max_grid_size=16只用于阶段正确性；性能作业617900和本轮619072均使用615364真实checkpoint的128³基础、1/10/33个块，max_grid_size=128，最小块为L1的32³和L2的32×32×64。不能将性能接近归因于16³小块；没有GPU计数器数据，也不能据此声称算力已满或未满。

619071在启动前取消以改申请单卡；619072最终SUCCEEDED，真实网格对照2次、性能4次应用退出码均为0。节点whshare-agent-178，A100-PCIE-40GB、单rank/GPU0。复用此前已验证的冻结候选程序，不是当前源码重新构建；程序SHA256、输入、命令和退出码见[作业目录](../../runs/ghost_realgrid_20261026/job619072/)。

off/on/on/off均从step256000固定原三层布局，BF32、regrid_int=-1，关闭输出、收敛和OSI细分计时；每次1600步至257600，每400步报告，排除首窗口，每模式6个稳态窗口。三层调用次数和launch_cells逐窗口相同；候选仍按原区域发射，launch_cells不代表实际执行碰撞的cell数。

| 每400步稳态均值 | 关闭跳过 | 开启跳过 |
|---|---:|---:|
| total(s) | 14.505682 | 14.491630 |
| collide(s) | 9.853043 | 9.844810 |
| MLUPS_total | 1462.349 | 1463.767 |
| total窗口范围(s) | 14.499284–14.514466 | 14.485818–14.497333 |
| L0 collide(s) | 0.301010 | 0.300813 |
| L1 collide(s) | 1.218241 | 1.225729 |
| L2 collide(s) | 8.333793 | 8.318268 |

总时间下降0.0969%，碰撞时间下降约0.0836%；本次正反顺序均有极小收益，但不足以作为实用优化采纳理由，不写成两种方案严格同速，也不能从中推导GPU利用率瓶颈。此前批量区域和坐标复用候选分别见[批量实验](2026-10-23-collision-batch-experiment.md)、[计数器与坐标实验](2026-10-23-collision-profile-coordinates.md)，均未合入。

真实网格推进32步至256032的DDF对照：三层active（比较器定义为covered==0）的Linf/L1/L2均为0；L2全valid范数也为0。L0/L1全valid的L1/L2范数为NaN，不能据valid_linf=0或global active范数为0声称全valid逐值通过。日志ddf_mismatch_location标记均落在covered、interface=0；该计数的实现也会将NaN纳入count_gt_1e-12，不能把它直接解释为有限值误差数。是否为两端相同的既有非有限模式、是否在源checkpoint中已存在，尚未逐值分类验证，保持pending。

本轮为历史候选复测，正式源码未改、候选未启用。输入/命令/原始日志/summary.json/audit.json/程序/checkpoint及源码补丁作为关键复现材料保留。[参数化入口](../../scripts/temporary/test_ghost_collision.sh)支持GHOST_END_STEP、GHOST_REPORT_INT、GHOST_REAL_CHECK、GHOST_RESULTS_DIR；[汇总工具](../../scripts/temporary/analyze_ghost_collision.py)剔除首窗口并按层输出，均仍服务复现。

### 知识收尾边界（2026-10-26）

- 代码：verified-current，仅核对三项实验开关均未进入正式src/config；用户已有5个源码修改文件保留，不纳入本轮验收或提交。
- 运行态：verified-current，619072成功；全valid非有限模式验证为pending，Nsight计数器权限仍pending。
- 文档：changed-and-verified，本记录为ghost复测权威结果；当前状态和MLUPS记录仅放摘要/指针，不改写历史性能表。
- 规则：verified-current，根AGENTS.md→CLAUDE.md软链有效；本轮未改规则。新实验按现有runs/temporary约定，已有证据目录不自动迁移。
- 记忆：out-of-scope（generated-read-only），只读查线索，本次未明确授权写记忆，不修改生成文件。
- 工作区：证据保留；其他算例、用户修改、性能/数值修复均out-of-scope。用户在收尾汇报后明确确认“删除候选清理”，2026-10-26已删除ghost_collision_20261023、collision_batch_20261023、collision_coordinates_20261023下的candidate/tmp_build_dir（三个各约139MiB）。删除前确认仅含o/s编译中间产物、无跟踪文件/软链且实验入口不依赖这些目录；删除后检查源码快照、config和可执行文件均保留。日志、checkpoint、报告及复现脚本/补丁未删除。
