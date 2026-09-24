# BOX3D_OSI 当前交接状态

更新时间：2026-09-24

## 现役配置

权威参数在 `config/inputs`：`amr.max_level=2`、`amr.regrid_int=32`、
`amr.max_grid_size=128`、`lbm.stream_mode=1`、`lbm.collide_mode=1`、
`lbm.osi_local_direct=1`、`lbm.osi_parallel_copy=1`、`lbm.osi_mpi_direct=1`、
`performance.report_int=1000`、`max_step=128000`。默认是 OSI 单数组路径；A-B 基准
需显式覆盖 `lbm.stream_mode=0`。这些是当前工作树中的未提交输入改动，
不是已验收的生产默认值。当前还设有 `lbm.osi_mpi_device_direct=1` 和
`lbm.osi_mpi_pipeline_chunk_bytes=1`；HMPI/UCX 上 device-direct 能力门禁会拒绝
不支持的 GPU-aware MPI 配置。已有 host-staging 对照须按对应运行快照读取覆盖参数，
不能直接用此工作配置复现。历史单 peer 测得较优 chunk 为 2097152（2 MiB），
并非当前工作配置。OSI 地址使用预计算 phase shift；Boundary
保留坐标缓存，碰撞显式坐标缓存和 branchless 分支均未保留。

主提交入口 `scripts/submit.sh` 以 `config/inputs` 为唯一当前工作配置，并在启动前复制
到 `runs/<timestamp>_job<job-id>/inputs`。程序从该独立目录运行，PlotFile 使用 `plt_`
前缀，checkpoint 使用 `chk` 前缀；同目录还记录命令行覆盖、完整命令、Git commit、
可执行文件 SHA256 和运行日志。当前输出间隔为 `amr.plot_int=3200`、
`checkpoint.chk_int=32000`。专项 `submit_*.sh` 仍采用各自的历史工作目录约定，不属于
主入口的运行快照合同。

## 最新诊断（2026-09-24）

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
  全 valid 差值误作 uncovered 差值；真正的阶段边界还需逐点复测。
- 独立 A-B/OSI 作业 `603370`/`603371` 使用相同的 inputs 快照，在 step 33
  level 0 的 `Boundary` 返回后、`Swap` 前读取 uncovered 格点 `(1,111,111)`、q=18：
  A-B 为 `0.018777451872808406`，OSI 为 `0.018585093245395951`，差
  `0.00019235862741245544`。两份日志均报告 `covered=0`，因此此阶段
  uncovered 区域仍存在差异；单点测量不代表该区域的全局最大差。
- job `603355` 的新细层对照使用诊断内部构造的参考态，其 level 1 重构后
  `linf=0` 只证明两套初始化计算一致；后续锁步细层参考态产生 NaN，不能将
  该诊断当作独立 A-B 模式的完整步进结果。jobs `603362`、`603364`、
  `603365`、`603368` 的细层锁步数据均受此问题影响。
- jobs `603298`/`603305` 的早期主机逐点结果**无效**：手写速度表的 q=22、q=26
  z 分量有误，修正后才得到上述零差。旧批次范数诊断在相同测点报告的
  `0.07579002442` 也是假差异：逐 q 范数曾给出 `DBL_MAX`，而同分量的
  min/max 均为 0。其归约/比较路径仍待查；现已移除误导性的自动 A-B 范数输出。
- 当前 `CommunicateOsiLevel()` 直接调用 `CommunicateOsiLevelLocalDirect()`；
  `osi_local_direct=0` 不会切回旧的整层 canonical 通信。当前
  `AverageDownOsiValidLevel()` 用函数局部 Q 分量 canonical `MultiFab` 完成
  restriction，已不使用共享 `osi_sync_buffer`；这也不等于整个 OSI 生命周期
  完全不使用该缓冲。旧计划文档中的 fallback 叙述仅代表其编写时的实现。
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

下一次诊断应使用完整有效的 A-B 细层参考态，在 step 33 的各阶段复用逐点
比较，定位 uncovered 首次差异，并检查 covered 邻域数据是否被 streaming
读取；同时核对旧 GPU 范数诊断的计算与归约实现。保留现有日志、
PlotFile、checkpoint 和输入快照供复核。

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
  单 peer、每 rank 约 16 MB payload 下 2 MiB 是已测最佳值。这是显式性能选项，
  未改为生产默认。

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

## 结论边界与待办

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
- `AverageDownOsiValidLevel()` 仍在 restriction 前后将 fine/coarse valid DDF 分批
  解码到 `osi_sync_buffer`，再调用 AMReX `average_down`，因此多层平均主路径尚未
  完全 OSI-native；
- regrid、checkpoint/state 重建和诊断中的批次缓冲使用属于独立生命周期，不能据此
  宣称生产 OSI 路径已经彻底移除 `osi_sync_buffer`。

因此，下一项代码工作应优先替换 `AverageDownOsiValidLevel()` 的 canonical
restriction 中转；在该项完成并通过逐 cell/逐 q 验收前，不应删除成员或把文档写成
“OSI 模式完全不使用 `osi_sync_buffer`”。

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
