# BOX3D_OSI：A-B 与 OSI 阶段误差证据表

更新日期：2026-09-24。这里的“误差”指相同输入下 A-B 与 OSI 的**逻辑 DDF 或输出数值之差**；同一模式从碰撞前到碰撞后的正常数值变化不算误差。`一致`只表示表中写明的区域和时间步通过了对应检查，不代表整场、后续时间步或多 rank 已验证。`近一致`表示存在约 `1e-16` 的浮点差。`未判定`表示缺少可用于归因的独立双作业比较。

## 当前修正后的阶段检查

| 时间与阶段 | 比较区域、方法 | 结果 | 证据与限制 |
| --- | --- | --- | --- |
| step 32 新 level 1 初始化后 | 已导出的同层 ghost 及其 valid 源，共 66,048 个单元 × 27 分量 | **近一致**：最大差 `2.78e-17` | [修正后 job 603471/603472 比较](../runs/20260924_210028_job603471/fixed_trace_comparison.txt) 中 `initial` 行；不是全部新层 valid 的独立逐值比较。 |
| step 32 首次 `FillGhostLevel(1)` 后 | level 1 粗层插值写入的 135,200 个 ghost 单元 × 27 分量，独立 A-B/OSI 导出 | **一致**：3,650,400 个值逐值相等 | 修正后的 [A-B job 603471](../runs/20260924_210028_job603471/fixed_trace_comparison.txt) / [OSI job 603472](../runs/20260924_210036_job603472/command.txt)。另有 66,048 个同层源/ghost 单元最大差 `2.78e-17`。不等于全部细层 valid 均已独立逐值验证。 |
| step 32 level 1 首次细步碰撞后、通信前 | 上述全部插值 ghost，以及同层 ghost 的 valid 源 | **插值 ghost 一致；同层源近一致**：分别为零差和最大差 `1.11e-16` | [通信前逐值统计](../runs/20260924_210028_job603471/fixed_trace_comparison.txt) 中 `before` 行。该导出未覆盖全部细层 valid。 |
| step 32 level 1 首次 `CommunicateLevel` 后 | 66,048 个同层 ghost 和 135,200 个插值 ghost，独立 A-B/OSI 导出 | **同层 ghost 近一致，插值 ghost 一致**：最大差分别为 `1.11e-16`、零 | [通信前后逐值统计](../runs/20260924_210028_job603471/fixed_trace_comparison.txt) 中 `before`/`after` 行。两种模式的同层 ghost 在通信前后均与各自 valid 源逐值相等；仅覆盖单 rank 首个细步。 |
| step 32 level 1 两个细步的 `Stream` 后 | 锁步参考态的 uncovered valid，逐阶段比较 | **一致**：两个细步的 Initial、Collision、Communication、Stream、Boundary、Swap 均报告 `linf=0` | [修正诊断后的 job 603503](../runs/20260924_231109_job603503/run.log)。诊断现为参考态填充粗层插值 ghost，并按 `nghost=2` 碰撞、迁移；此行是锁步对照，独立双作业的全部细层 valid 尚未导出。 |
| step 32 首次 `AverageDownInterfaceLevel(0, true)` 出口 | level 0 全部 uncovered 和 interface，独立双作业逐值导出 | **uncovered 一致；interface 近一致**：最大差分别为零和 `2.22e-16` | [job 603473/603474 比较](../runs/20260924_210127_job603473/fixed_average_comparison.txt)。此时 interface 入口曾有最大差 `0.004901078005466977`；出口近一致不证明细层其余 valid 或后续推进一致。 |
| step 33 level 0 `Boundary` 后 | 已知 uncovered 点 `(1,111,111), q=18` | **该点一致**：均为 `0.018585093245395951` | [A-B job 603478](../runs/20260924_210611_job603478/run.log) / [OSI job 603479](../runs/20260924_210617_job603479/run.log)。这是单点，不是全域结论。 |
| step 250、500、750、1000 的 PlotFile | 速度、密度、涡量保存值逐值检查 | **出现严重数值错误**：A-B 三层均有限；OSI 从最早保存的 step 250 起仅剩 level 0，保存值全部为 NaN | 同一输入快照与可执行文件，仅 `lbm.stream_mode` 不同：[A-B job 603486](../runs/20260924_215616_job603486/finite_check.txt) / [OSI job 603487](../runs/20260924_215621_job603487/finite_check.txt)。首次产生 NaN 的具体步数、阶段尚未测出。 |

## 独立 OSI 与锁步诊断中的 OSI

| 时间与对象 | 结果 | 证据与限制 |
| --- | --- | --- |
| step 32 结束时，level 0/1 checkpoint 中的网格 Header 和全部已保存 valid DDF | **逐字节相同** | [独立 OSI job 603505](../runs/20260924_231932_job603505/) 与 [锁步诊断 job 603506 的对比记录](../runs/20260924_232007_job603506/osi_vs_oracle_checkpoint_comparison.txt)。两者输入快照、可执行文件相同，仅 `verification.osi_ab_check` 开关不同。checkpoint 不保存 ghost；此结论不覆盖 step 33 以后。 |

源码中的锁步分支对 OSI 调用与独立模式相同的 `Collide`、`CommunicateLevel`、`Stream`、`Boundary`、`SwapLevel`；额外执行 A-B 参考态操作及逐阶段检查。上述 checkpoint 对比验证了 OSI 的已保存 valid 结果在首次重网格和两个细步后没有被诊断模式改变。它不能证明额外操作对后续所有步都没有影响，尤其锁步参考态的平均下传尚未全面对齐。

## 已修正的历史差异与无效报警

| 观测 | 当时状态 | 当前解释 |
| --- | --- | --- |
| step 32 入口、边界修复、首次重网格后，level 0 全部 valid | **当时一致**：三处主机逐值检查均为零差且有限 | [job 603307](../runs/20260924_121350_job603307/run.log) 属于初始化修正前版本，仅证明该版本的 level 0，不代替修正后的新 level 1 整场结果。 |
| step 32 新 level 1 的初始 valid 值不同，例如 `(1,128,253), q=4` 为 A-B `0.072906735842454579`、OSI `0.076610337500803594` | **当时确有差异** | A-B 新层曾走 `FillCoarsePatch()`，OSI 走 `FillNewLevelFromCoarse()`；现已统一为后者。修正前数据不能代替当前结果。详见 [当前状态](current_status.md)。 |
| 锁步 job 603490 在 level 1 `Stream` 后报差 `3.8184455009127732e-05` | **已修正的诊断参考态假差异** | 来源 ghost `(12,25,223), q=13` 在独立 A-B/OSI 中均从插值后的 `0.018516828839631985` 经碰撞变为 `0.018555013294641112`；该变化量恰好等于报警差。旧锁步 A-B 参考态的 ghost 未参与碰撞，保留旧值；修正后 [job 603503](../runs/20260924_231109_job603503/run.log) 的两个细步均未报差。 |
| 修正前首次平均出口 interface 最大差 `0.2930981606775104` | **历史版本有差异** | 当时新层初值和 OSI 回退平均写回范围尚未修正。修正后的首次平均出口最大差为 `2.22e-16`；不要混用两个版本的结果。详见 [当前状态](current_status.md)。 |

## 尚未定位的边界

当前可确认的是：修正后的单 rank 短窗口中，已导出的首次插值 ghost、碰撞后通信输入和通信输出没有出现可解释旧 `3.82e-5` 报差的 A-B/OSI 差异；修正锁步诊断后，step 32 的两个细步 valid 也通过阶段比较；首次平均出口的 level 0 uncovered/interface 一致或近一致。**锁步参考态的界面平均和后续重网格路径尚未全面对齐，独立双作业也尚未在每个时间步导出全部层级 valid 与 Stream 出口**，所以不能断言 OSI 从 step 32 到 step 250 的首次真实分叉发生在哪个函数。step 250 的 PlotFile 已全为 NaN，只给出了首次观察到非有限输出的上界。

## 证据索引

- [修正后通信前后原始导出及比较脚本：job 603471](../runs/20260924_210028_job603471/)；[OSI job 603472](../runs/20260924_210036_job603472/)。
- [修正后首次平均入口/出口比较：job 603473](../runs/20260924_210127_job603473/)；[OSI job 603474](../runs/20260924_210202_job603474/)。
- [当前生产可执行文件的 1000 步 A-B 输出：job 603486](../runs/20260924_215616_job603486/)；[OSI 输出：job 603487](../runs/20260924_215621_job603487/)。
- [锁步诊断 job 603490](../runs/20260924_220522_job603490/) 仅用于说明报警为何无效。它与两个 1000 步作业的可执行文件 SHA256 相同，但开启了 `verification.osi_ab_check=true`，改变了内部参考态执行路径。
- [修正 ghost 填充及推进范围后的锁步诊断 job 603503](../runs/20260924_231109_job603503/) 使用重新编译的可执行文件，仅验证到 step 32。
