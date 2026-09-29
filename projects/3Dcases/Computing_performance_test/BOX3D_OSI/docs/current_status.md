# BOX3D_OSI 当前交接状态

更新时间：2026-09-29

## 当前数值与长程运行结论（2026-09-29）

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
`scripts/submit_osi_lockstep_alllevels_64.sh`。

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
  `cell_cons_interp`、无非平衡缩放），OSI 调用 `FillNewLevelFromCoarse()`
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
