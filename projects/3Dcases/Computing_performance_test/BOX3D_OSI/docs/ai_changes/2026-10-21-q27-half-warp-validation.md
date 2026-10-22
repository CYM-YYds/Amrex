# Q27 半 warp：实际平均路径验证

历史实验记录：下文“当前/正式/未采纳”指本报告对应的冻结版本。后续映射已在`7c4a034`合入；现役实现及验收见[生产合入记录](2026-10-22-average-mapping-production-merge.md)，不改写本报告原始测量。

目的：验证独立微基准中 Q27 半 warp 的收益能否延伸到真实 BOX3D_OSI 平均阶段和总推进。
范围：仅改变 OSI 界面 restriction 的线程组织；生产输入、碰撞、插值、AMR 判据、通信与物理边界公式不变。A-B 平均仍用原实现。

## 实际结果与判定

**当前实装没有把微基准收益转化为生产配置收益，保留原整 warp 默认路径。** 30个运行全部正常退出；210次同源逐值检查的最大误差为0，四组64步最终checkpoint的所有valid DDF误差也为0。性能对照每个窗口的每层calls/launch_cells均一致。

成功作业616312，`whshare-agent-36`，NVIDIA A100-PCIE-40GB；初始两张GPU均无其他进程。[完整比较表](../../runs/q27_half_validation_20261021/job616312/comparison.md)、[机器可读结果](../../runs/q27_half_validation_20261021/job616312/summary.json)、[逐组退出状态](../../runs/q27_half_validation_20261021/job616312/status.tsv)。

下表为各运行丢弃前256步后的窗口；耗时增加百分比先逐对计算，再取中位数。restriction单独计时包含主机启动调度和GPU完成等待，其额外同步使该组不能与主性能组混合。

| 对照组 | 重复对数 | 指标 | 半warp耗时增加中位数 | 各对范围 |
|---|---:|---|---:|---:|
| 1 rank主性能 | 5 | average | +2.957% | +2.829%～+2.984% |
| 1 rank主性能 | 5 | total | +0.287% | +0.173%～+0.372% |
| 1 rank restriction分析 | 3 | average_restrict | +4.003% | +3.593%～+4.042% |
| 2 rank同步host staging | 3 | average | +5.098% | +4.689%～+5.169% |
| 2 rank同步host staging | 3 | total | +0.823% | +0.726%～+0.897% |

包含第一个窗口时，单rank主性能的average增加约2.317%、total增加约0.199%；双rank的average增加约3.783%、total增加约0.509%。无论是否排除启动窗口，方向均未反转。

结论仅针对当前256线程/block、运行时缩放参数的实装与上述短程输入。数值相同不等于长期稳定性/收敛证明；性能退化也不否定其他block大小、分派方式、网格配置或GPU上的半warp实现。

## 实验材料与构建

- 源码基线 `13d77f9`，实验前工作区干净。
- [完整源码差异](../../runs/q27_half_validation_20261021/experiment.diff)、[原源码](../../runs/q27_half_validation_20261021/baseline_src/)、[实验源码](../../runs/q27_half_validation_20261021/experiment_src/)。
- 基线通过原编译入口确认 up-to-date；实验版本完整编译，保存 [构建日志](../../runs/q27_half_validation_20261021/experiment_build.log)。使用生产 GNUmakefile、CUDA 12.8/GCC 11.3、SM80、FP64、RDC、`--use_fast_math`。
- 实验状态只有构造时读取的 `verification.restriction_group=32/16`、`restriction_compare`、`restriction_timing`，生命周期内不更新，不新增布局缓存。
- 扩展已有启动器的分组宽度模板；保持 256 threads/block。32 线程组每 block 8 个父单元，16 线程组每 block 16 个。
- 半 warp 每线程持有 `q=lane` 和 `q=lane+16` 两个标量槽；归约 mask 和 width 限制到各自 16 线程组，尾部完整分组退出。ratio、scale、is_scale 仍由实际调用传入，没有假设生产 ratio 是编译期常量。
- 校验缓冲仅在双算开关启用时分配，局限于一次界面平均调用。两条路径读取同一个实际 fine Fab/ring/phase，逐点比较全部 Q 个目的分量并拒绝 NaN/Inf；局部归约避免无本地界面盒的 MPI rank 引发 collective 不匹配。
- [SASS 对照](../../runs/q27_half_validation_20261021/codegen_comparison.json)：去除符号名后，原可执行文件与实验整 warp 路径的 872 条指令完全一致；半 warp 为880条静态指令。静态指令数不是动态指令数或性能证明。
- 实际 CUDA 内核资源：整 warp 63 registers/thread，半 warp 64；两者 stack/local 均为 0。见 [资源报告](../../runs/q27_half_validation_20261021/resources.txt)。
- 基线原件已保存；实验开始后已恢复正式源码并再次通过原入口重建正式可执行文件；实验只使用独立冻结的 `experiment.ex`。本次不把实验开关或替代内核留在正式源码中。

## 数值验证与归约顺序

四组输入为 1/2 rank × 非周期 cavity/全周期。基础网格 64³、max_level=2、max_grid_size=32、regrid_int=32，使用现有 `regrid_fixture`，各推进 64 coarse steps。周期输入启用现有非均匀 seed pattern。
每组先用原版本生成最终 checkpoint，再运行半 warp，并在每次实际界面平均中与整 warp 做同源双算。最终 checkpoint 使用已有逻辑坐标逐 cell、逐 q 比较器核对所有 valid DDF（含 covered），另输出 active 范数。
同源界面误差和最终 checkpoint 误差阈值均为 `2e-14`；这与 A-B/OSI 历史物理 ghost 差异是不同的比较。

这套半 warp 配对可以保持原整 warp 的归约树：

```text
整 warp：32 lane，先 offset=16 相加 → offset=8 → 4 → 2 → 1
半 warp：每 lane 先把 q 与 q+16 相加 → offset=8 → 4 → 2 → 1
```

因此“半 warp 必然改变浮点求和顺序”的泛化不适用于这套配对；仍需实际代码生成和逐值证据，不能由示意式代替验证。

## 性能比较口径

成功运行的状态与数值结果、每对性能结果见上方实际结果和完整机器可读报告。

- 使用同一个实验可执行文件，只切换 `restriction_group`；数值校验运行和计时运行分开。
- 主性能：生产 inputs 的 Re=3200、128³、max_level=2、max_grid_size=128、regrid_int=32、默认非周期腔流；1 rank/GPU，5 对正反交替，分别推进 1024 步。
- restriction 分析：同一生产输入，3 对交替、各 512 步，单独开启 `restriction_timing`。该计时在 restriction 末尾额外同步，不与主性能混合统计。
- MPI：2 rank/GPU、同步 host staging（`osi_mpi_pipeline_chunk_bytes=0`），max_grid_size 改为64，3 对交替、各512步；该配置的结果不外推到生产默认的2 MiB流水或device-direct MPI。
- 计时均关闭 `osi_detail_timing`、双算校验、PlotFile、checkpoint 和收敛停止；该开关关闭后 `osi_*`=0 表示未测量，不是没有成本。
- 每256步一个统计窗口。分别报告全部窗口，以及丢弃第一个256步窗口的结果；后者仅排除启动窗口，不代表流动达到物理稳态。
- 多 rank 每个指标/窗口取较慢 rank，再对窗口求和；不把 rank 时间相加。每对检查每层碰撞 calls、launch_cells 完全相同。
- `average` 包含 restriction 和实际复制/通信；`total` 包含重构、不含初始化。结果是短程性能对照，不是长期收敛或跨节点通信证明。

## 复现与收尾

独立快照包含输入、源码、原/实验可执行文件、SHA256、提交脚本、构建日志、实际命令和原始输出。从算例目录提交：

```bash
dsub -s runs/q27_half_validation_20261021/submit_validation.sh
```

提交脚本从当前算例目录定位冻结材料，在独立 `job<jobid>/` 下重跑，不覆写历史结果。
实验脚本属于本次 AI 临时入口；结果完成后保留快照内的独立复现副本，删除无后续依赖的临时入口，保留所有运行数据和诊断证据。

## 对微基准与实际路径的解释边界

独立微基准的半warp使用编译期缩放分派，本轮实装保留生产接口的运行时bool；本轮结论限定于当前实装，不外推到其他分派或block配置。独立微基准证明其输入/盒形/启动环境中的局部收益，不能代替真实平均阶段的比较。真实运行还包含稀疏盒的主机调度、实际数据与相位、MPI分布和复制。
数值校验的单 rank、64³输入共记录5988次界面盒调用，最大每盒256个父单元；约34.1%的盒调用不超过32个父单元。该直方图只描述这组数值校验，不是128³性能运行的完整盒分布。
固定256线程/block时，对256父单元盒，整warp启动32个block，半warp启动16个；对15父单元盒，则由2个block减至1个。减少分组宽度同时减少block数，可能影响小盒并行度。该机制是代码层面的解释候选，尚无硬件profiler证据证明它是主要瓶颈。
若继续研究半warp，下一项针对性假设应是128线程/block、每block仍处理8个父单元，以分开“分组宽度”和“父单元/block”的影响。本轮不把它算作已经验证的优化。

汇总命令：

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/q27_half_validation_20261021/summarize_q27_half_validation.py projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/q27_half_validation_20261021/job616312
```


## 2026-10-21 知识收尾

范围为本轮平均实验，不扩大到整个仓库或其他续跑任务。README 已指向 current_status，无需再复制结论；
current_status 补现役采纳判断，架构修正 coarse/fine staging 示意，MLUPS索引与早期微基准报告补后续验证指针。
权威分工为：current_status 给现役入口，本报告给Q27实际路径证据，微基准报告保留Q19/Q27局部实验历史。

| 事实面 | 状态 | 核对 |
|---|---|---|
| 代码/配置 | verified-current | 正式src与实验前baseline_src逐文件相同；三维Q27、32线程warp/父单元、256线程/block，无实验开关。 |
| 运行证据 | verified-current | job616312的30项退出码均0，210次同源检查与四组checkpoint误差均0；所有配对工作量相同。未重跑CFD。 |
| 文档 | changed-and-verified | 主交接、架构、性能索引与两个报告互相对齐；修正重建后三维程序仍与旧manifest同SHA256的过期说法。 |
| 规则 | verified-current | 根AGENTS.md仍为指向CLAUDE.md的有效软链；范围内无更近规则，无需增加重复规则。 |
| 记忆 | out-of-scope | Codex生成记忆只读；未获直接写入请求，不写更新注记或生成索引。 |
| 工作区 | verified-current | 单一worktree；本轮临时入口已移除，复现快照及全部日志/checkpoint保留，无本轮新增删除候选。 |

恢复重建后的当前三维程序SHA256为 `0b85e59b0138a65a416c9c5202de2f1547d5e51a68f0f9d92a0e1931a2166333`，与616234/616235及本轮baseline.ex的
`c5cb56aa5df684e37699638e48580ef35593ec79f7f5d326fc4ad7150fc77f67` 不同；不混用程序身份和数值验收范围。
实验源码、可执行文件、构建资料的SHA256复核通过；规则同源、引用与新增文档锚点检查通过。

保留分类：`runs/restriction_616311` 是微基准复现材料；`runs/q27_half_validation_20261021` 是实际路径源码、程序和逐值checkpoint证据；
`runs/restriction_616309/616310` 保存失败构建的错误与环境。它们都不是本轮可直接清场的残留。
其他任务的 `submit_restart_memory.sh`、`summarize_restart_memory.py` 未动。

pending：128线程/block及其他分派方式、Q19完整推进、硬件profiler瓶颈归因未验证；既有I/O、收敛及physical ghost问题不因本轮收尾而消失，仍以current_status为准。
out-of-scope：其他算例/dev归档改动、全仓库内容审计、结构审查、记忆写入、远端推送与历史数据清理。
not-applicable：服务部署或页面/API验收。warning：当前Git仍为detached HEAD；本次只做本地文档提交，不宣称已推送或合并。
没有删除候选或待清场动作，不需要为保留科研复现材料请求清理确认。
