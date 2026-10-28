# BOX3D_OSI

基于 AMReX 26.06、C++20、CUDA 和 MPI 的二维 D2Q9 / 三维 D3Q27 LBM 动态 AMR 算例。
原 BOX3D_OSI_dev 的维度兼容实现已合入本目录，后续统一在此维护；
原 dev 目录已在 `53de250` 删除，历史内容从 Git 获取。
本目录支持 canonical 双数组 A-B 基准和 one-step index（OSI）单数组推进；
相邻 `BOX3D` 是另一套基准算例。本文说明当前文件架构与使用入口，详细数值证据见
[当前交接状态](docs/current_status.md)，算法见 [OSI 架构](docs/osi_algorithm_and_architecture.md)。

更新时间：2026-10-22。


## 二维与三维统一入口

默认三维，现有 `config/inputs`（Re=3200）、三维 `state_error_6` 加密判据、
监护脚本和历史运行均保留。二维使用 dev 已验证的 `state_error_3` 判据与
[config/inputs.2d](config/inputs.2d)（Re=1000）。维度由构建宏唯一决定：

```bash
./scripts/compile.sh
DIM=2 ./scripts/compile.sh
dsub -x 'all;DIM=2;' -s ./scripts/submit.sh
```

三维提交仍为 `dsub -s ./scripts/submit.sh`。两个维度均保留独立运行目录、
输入快照、程序 SHA256、rank 退出记录和 `BOX3D_WALL_SECONDS` 监护。
编译数据库只包含当前构建维度；切换维度后重启 clangd。

二维顶盖在 y 高端，三维在 z 高端，沿 x 方向运动；二维平均四个子单元，
OSI 内部 z=0、z 环长=1。二维支持流体和静态圆盘，IBM、运动、SGS 与 cumulant
仍限三维。checkpoint 新增格子元数据检查；无元数据的旧 checkpoint 仅允许三维 D3Q27/order=0。

验证工具沿用 dev 的生产内核与逐值诊断：

```bash
bash tests/run_dimension_kernels_test.sh
bash tests/run_osi_index_test.sh
DIM_REFACTOR_BASELINE=/绝对路径/含app2d.ex和app3d.ex及git_head.txt的基线目录 \
  dsub -x 'all;' -s tests/submit_dimension_refactor_validation.sh
```

其余工具的基线接口如下；默认历史路径不保证在主算例中存在，提交前显式指定并核对文件：

| 工具 | 基线输入 |
|---|---|
| `tests/submit_dimension_validation.sh` | `DIMENSION_BASELINE_EXE`：迁移前三维程序完整路径 |
| `tests/submit_particle_geometry_validation.sh` | `PARTICLE_BASELINE_EXE`：粒子改动前三维程序完整路径 |
| `tests/submit_dimension_refactor_validation.sh` | `DIM_REFACTOR_BASELINE`：含 `app2d.ex`、`app3d.ex` 和 `git_head.txt` 的目录 |
| `tests/compare_dimension_codegen.py` | 位置参数：访问器重构证据目录，含 `baseline/src`、`baseline/objects{2,3}d` 及 `baseline/app{2,3}d.ex`；这是该次访问器重构的专用比较，不能直接用任意旧程序替换 |

历史 dev 内容可从 `53de250^` 的
`projects/3Dcases/Computing_performance_test/BOX3D_OSI_dev/` 路径读取，
例如在仓库根执行 `git show 53de250^:projects/3Dcases/Computing_performance_test/BOX3D_OSI_dev/README.md`。
不要假设历史基线程序或 runs 已迁入本算例；现役验证范围和已知 physical ghost 差异见
[当前交接状态](docs/current_status.md)。
历史专项预设（锁步、Re=1000、固定网格）仍按三维参数维护；二维运行使用上面的通用提交入口，不能仅设置 DIM 就套用三维预设。

合并版本的验证见 [合并记录](docs/ai_changes/2026-10-20-merge-dimension-support.md)。
现役配置、程序与较新实验范围见[当前交接状态](docs/current_status.md)，不把合并时的验收自动扩大到后续修改。

## 单精度与双精度实验

默认 `PRECISION=FLOAT`，场数据 `amrex::Real` 使用单精度；可显式选择 `DOUBLE`，
适用于两种维度。不能通过运行时 inputs 切换。以下命令在本算例目录执行：

```bash
# 默认单精度编译与提交
./scripts/compile.sh
./scripts/submit_guarded.sh
# 显式选择双精度；提交时也须将选择传给调度器
PRECISION=DOUBLE ./scripts/compile.sh
./scripts/submit_guarded.sh -x 'all;PRECISION=DOUBLE;'
# 二维默认单精度
DIM=2 ./scripts/compile.sh
./scripts/submit_guarded.sh -x 'all;DIM=2;'
```

单精度程序名为 `main{2,3}d.gnu.FLOAT.TPROF.MPI.CUDA.ex`，双精度沿用原名；
AMReX 自动按维度和精度隔离对象目录，无须清理另一精度的产物。
提交入口按所选精度选择程序，运行目录中的固定程序链接指向实际选择的文件；
`manifest.txt` 记录精度和程序 SHA256，应用启动时打印实际 `Real` 类型与字节数。
编译数据库只对应最后一次正常采集的维度、精度，切换后重新加载 clangd。
`GEN_CCDB=0` 会跳过数据库更新，适合独立实验构建，不能据此判断 IDE 精度。

数值核、格子常量使用 AMReX `_rt` 字面量，计时和性能统计的 `double` 保留。
粒子属性继续使用独立的 `ParticleReal`（默认 double），此次没有切换粒子存储精度；
粒子与场的接口按真实类型传递。该版本不声称所有粒子、几何或 host 表达式均为 FP32。
现有 CUDA 编译器 `--use_fast_math` 设置保持不变。

生产收敛条件、AMR 阈值和已有严格阶段诊断容差保持原值，不能把单精度的偏差自动视为通过。
`tests/run_dimension_kernels_test.sh` 默认检查两种维度、两种精度：双精度沿用 `1e-12`，
单精度解析解/独立路径比较采用 `32 * epsilon * max(1, abs(expected))` 的舍入容差；
这只适用于该小规模内核测试，不能作为长程流场的误差验收阈值。
跨精度 checkpoint 转换只按本次明确验证的范围使用，不承诺粒子/运动刚体完整重启。
默认值变更见[单精度默认记录](docs/ai_changes/2026-10-28-default-float.md)。
精度切换的初版 GPU 验证、误差和限制见仓库根
[精度切换记录](../../../../docs/ai_changes/2026-10-27-box3d-osi-precision.md)。

## 目录与构建关系

```text
BOX3D_OSI/
├── GNUmakefile                  # 包装入口：设置 src 搜索路径，包含 config/GNUmakefile
├── config/
│   ├── GNUmakefile              # AMReX 版本、MPI/CUDA/TinyProfiler 等构建选项
│   ├── Make.package             # 编译的源文件和头文件登记
│   ├── inputs                   # 当前物理、网格、AMR、通信及输出配置
│   └── compile_commands.json    # clangd 编译数据库
├── src/
│   ├── main.cpp                # 启动、coarse-step 主循环、Cycle2 多层子循环
│   ├── AmrCoreLBM.H             # 唯一驱动类：接口、状态、缓存及共享辅助定义
│   ├── AmrCoreLBM.cpp           # 基础生命周期、密度/速度重建、输出和 checkpoint
│   ├── AmrCoreLBM_amr.cpp       # AMReX 回调、regrid、粗细网格缓存与插值
│   ├── AmrCoreLBM_advance.cpp   # 单层推进、边界、平均和布局分派
│   ├── AmrCoreLBM_osi.cpp       # OSI 编解码、同层通信、粗细传递与迁移
│   ├── AmrCoreLBM_diagnostics.cpp # 逐值对照、source 诊断、checksum 和收敛
│   ├── OsiAbVerification.H/.cpp # 将验证动作插入生命周期，不拥有生产状态
│   ├── OsiIndex.H              # Fab-local 地址映射和几何/相位上下文
│   ├── OsiCommunication.H      # 通信 tag、MPI staging 和交换执行骨架
│   ├── Kernels.H               # 单元级碰撞、迁移、边界及宏观量 kernel
│   ├── LbmLattice.H             # 统一速度集：DIM=2 为 D2Q9，DIM=3 为 D3Q27
│   ├── LbmGridParams.H         # 可传给 host/GPU 的运行时网格与物理参数
│   ├── LagrangeParticleContainer.H/.cpp # 粒子存储、插值和受力计算
│   ├── AuxiliaryPointContainer.H/.cpp  # 辅助测点容器
│   └── InitParticles.H         # 粒子初始化辅助函数
├── scripts/                    # 5 个用户入口与 5 个内部依赖
│   ├── archive/                # 12 个历史专项驱动及复现说明
│   └── experiments/cuda_aware/ # 独立 CUDA-aware MPI 三件套，未验收
├── tests/                      # 独立地址/覆盖测试及实验日志检查
├── docs/                       # 算法、状态、历史性能与 ai_changes 变更记录
├── data_post_processing/       # Python 后处理及历史图片
├── runs/                       # 独立实验目录：输入、命令、证据与输出
├── logs/                       # 编译和调度日志
├── tmp_build_dir/              # 构建中间文件
└── main{2,3}d.gnu.TPROF.MPI.CUDA.ex # 两个维度的可执行文件
```

其他 `chk*`、`data*` 目录属于历史结果，不能当作待删除构建残留。
`.clangd`、`.clang-format` 和 `.clang-tidy` 是开发工具配置。
`config/Make.package` 决定实际参与构建的文件；源码目录并不自动全量编译。

## 源码职责与数据所有权

五个 `AmrCoreLBM*.cpp` 实现的是**同一个类**，共享 `AmrCoreLBM.H` 声明的成员；
它们不是五个独立管理器。头文件中的 `Box3dDetail` 保存共享计时器、checkpoint
格式常量及批量边界任务类型，原 `AmrCoreLBM_detail.H` 已合并并移除。

| 文件 | 负责的工作与代表入口 |
| --- | --- |
| [main.cpp](src/main.cpp) | 读取启动配置、构造 Geometry/AmrInfo/驱动对象；初始化或重启；安排 regrid、`Cycle2`、输出与停止条件 |
| [AmrCoreLBM.H](src/AmrCoreLBM.H) | 声明 RuntimeParams、PerfStats、RunMode/DdfLayout；拥有 DDF、phase、宏观场、mask、粗细层与通信缓存 |
| [AmrCoreLBM.cpp](src/AmrCoreLBM.cpp) | 构造与参数读取、`ComputeMacroLevel`、PlotFile、`WriteCheckpoint`/`ReadCheckpoint`、粒子接口 |
| [AmrCoreLBM_amr.cpp](src/AmrCoreLBM_amr.cpp) | `MakeNewLevel*`/`RemakeLevel`/`ClearLevel`/`ErrorEst` 等 AMReX 回调；`RefineMesh`、覆盖 mask、插值/平均缓存、模板覆盖检查 |
| [AmrCoreLBM_advance.cpp](src/AmrCoreLBM_advance.cpp) | `AdvanceLevel`、Collide/Stream/Boundary/Swap；`FillGhostLevel`、`CommunicateLevel` 等布局分派；界面/完整平均及平均后物理边界修复；涡量与剪切 |
| [AmrCoreLBM_osi.cpp](src/AmrCoreLBM_osi.cpp) | `DecodeOsiValid`；`FillBoundaryOsi` 分派后的通信实现、通信缓存与 `ParallelCopyOsi` 重网格迁移 |
| [AmrCoreLBM_diagnostics.cpp](src/AmrCoreLBM_diagnostics.cpp) | 阶段级/逐 cell 对照、Stream 源诊断、checksum、checkpoint 对比和收敛测量；不是生产推进入口 |
| [OsiAbVerification.H/.cpp](src/OsiAbVerification.H) | 按 `verification.*` 配置安排 regrid 前后、首次 FillGhost 和推进后的检查；实际比较由驱动类诊断函数执行 |
| [OsiIndex.H](src/OsiIndex.H) | `FabGeometry`、`PhaseShift3`、`MakeOsiFabContext(ring, phase)` 和 `osi_address`；只计算索引，不拥有 level 状态 |
| [OsiCommunication.H](src/OsiCommunication.H) | local/remote copy、pack/unpack tag 和 MPI 交换公共骨架；具体区域与布局选择由 `_osi.cpp` 决定 |
| [Kernels.H](src/Kernels.H) | 单元级数值计算；由驱动层传入 Array4、几何、相位和物理参数并发射 kernel |
| [LbmLattice.H](src/LbmLattice.H) / [LbmGridParams.H](src/LbmGridParams.H) | 前者给出速度集、权重、Q、最大编译层级等模型常量；后者传递运行时尺寸、dx/dt 与黏度 |

A-B 状态是每层 `f_old/f_new`；OSI 生产状态是每层 `osi_state` 和 `osi_phase`。
开启 `verification.osi_ab_check` 时额外分配 A-B 状态用于锁步对照。
`ring` 必须来自完整 grown-Fab，不能用 tilebox、碰撞域或物理域裁剪后的 Box 替代。
相位属于 level 驱动，在 host 上准备 Fab 上下文，再供整个 kernel 复用。

新层 DDF 初始化只写 valid，并按目标所需 stencil 搬运 coarse 数据；初始化、
ghost 填充和重构迁移统一采用自定义数值插值核。AMReX 仍提供 stencil 几何。
范围、公式及最新验证边界见[当前状态](docs/current_status.md#2026-10-14-新层初始化与插值收尾)。

## 实际推进调用链

`main.cpp` 的现役入口是 `Cycle2`。每个 coarse step 先处理需要的 regrid，
然后进入多层递归；同文件中的 Rohde/Jaber 等其他周期函数不在当前主调用路径上。

```text
main coarse-step 循环
  ├─ regrid 步：AverageDownValid → RepairCurrentStatePhysicalBoundary → RefineMesh
  └─ Cycle2(lev, time)
       ├─ 有细层时：FillGhostLevel(lev, time, scale) 填 lev+1 的 ghost
       ├─ AdvanceLevel(lev)
       │    ├─ 串行路径：Collide → CommunicateLevel → Stream → Boundary
       │    ├─ OSI 重叠路径：发送源碰撞 → FillBoundaryOsi(剩余碰撞回调) → Stream → Boundary
       │    └─ SwapLevel
       └─ 有细层时：Cycle2(lev+1) 两次 → AverageDownInterfaceLevel(lev+1)
```

两个 fine substeps 共用父层在本次调用开始时的 ghost 填充。
普通推进只执行所选布局；锁步验证模式在 `AdvanceLevel` 内显式执行两种布局，
在碰撞、通信、迁移、边界等阶段比较；重叠时碰撞比较在回调内、两个子集都完成后执行。
发送源分类、stream 顺序和 phase 依赖的唯一机制说明见
[同层通信重叠](docs/osi_algorithm_and_architecture.md#61-整块碰撞与同层传输重叠)。
A-B 的 Stream 写入 `f_new`，随后 Swap；OSI 的 Stream 提交 phase，Swap 无需交换数组。
完整平均后的边界修复只改 covered 粗层 valid 物理边界，排除 uncovered、ghost 和最细层；
[算法文档](docs/osi_algorithm_and_architecture.md#101-完整平均后的边界修复)给出筛选细节。

| 要修改什么 | 先定位哪里 |
| --- | --- |
| coarse-step 顺序、细层子循环或输出时机 | `main.cpp` 与 `config/inputs` |
| 新建/重建 level、嵌套与粗细模板来源 | `AmrCoreLBM_amr.cpp` |
| 碰撞、边界或单层阶段顺序 | `AmrCoreLBM_advance.cpp` → `Kernels.H` |
| OSI 地址、phase 映射 | `OsiIndex.H`；phase 的提交点在 `_advance.cpp` |
| OSI MPI、粗细 raw/canonical 传递 | `AmrCoreLBM_osi.cpp` → `OsiCommunication.H` |
| 增加逐值检查及检查时机 | `_diagnostics.cpp` 与 `OsiAbVerification.H/.cpp` |
| 参数、checkpoint 格式或密度/速度输出 | `AmrCoreLBM.cpp` |

## 构建、运行与实验记录

在本算例目录执行：

```bash
./scripts/compile.sh
./scripts/submit_guarded.sh
```

`compile.sh` 转发到仓库级编译脚本，在编译节点加载环境；构建采用 AMReX 26.06、
MPI、CUDA、TinyProfiler，项目要求 C++20 和 GCC 11+。
主提交脚本创建 `runs/<timestamp>_job<job-id>/`，冻结 `inputs`、`overrides.txt`、
`command.txt`、`manifest.txt`，并保存 `run.log`；相对 PlotFile/checkpoint 路径落在该目录。
它记录可执行文件 SHA256，但程序使用指向算例可执行文件的符号链接；不能把该流程
描述为冻结了独立可执行副本或完整源码快照。严格版本对比应另存 app 与源码证据。

修改生产配置使用 [config/inputs](config/inputs)。专项测试可通过 `AMREX_RUN_ARGS`
覆盖参数，主提交脚本会记录原始字符串。顶层保留日常入口和内部依赖；历史专项实验见
[归档索引](scripts/archive/README.md)，未验收的独立 MPI 流程见
[CUDA-aware 实验索引](scripts/experiments/cuda_aware/README.md)。它们有不同的快照和运行目录机制，不能一概而论。
依赖已缺失 `config/inputs_osi` 的四个历史 stage 提交入口已退役。
专项验收需使用完整输入快照和明确的覆盖参数，不能直接套用历史命令。

短程加密诊断可设置 `verification.refinement_diagnostics=true`（默认关闭）：
每次 ErrorEst 输出实际读取的 valid 涡量范围、均值、非有限值和固定阈值超限比例，
并在 regrid 后复用 `REGRID_DIAG` / `CF_MASK_DIAG` 输出打标数及网格覆盖信息。
统计包含 covered 粗单元、排除 ghost，与实际打标遍历一致；不修改 tag，
也不启用 A-B 双数组验证。诊断含额外 GPU 归约和 MPI 同步，不用于性能比较。

续跑时给主提交脚本设置 `RESTART_CHECKPOINT` 为 checkpoint 完整路径；
脚本在新运行目录创建 `chk<step>` 源链接并设置 begin_step，后续 checkpoint 写在新目录。
OSI checkpoint 保存 canonical DDF，读取后 phase 从 0 开始。当前 checkpoint 写出与同布局新层初始化共用 `DecodeOsiValid`，
不同布局重网格仍使用 `ParallelCopyOsi`。

Python 后处理统一使用仓库根 `.venv/bin/python`。例如从仓库根运行：

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/data_post_processing/plot_convergence.py \
  --csv projects/3Dcases/Computing_performance_test/BOX3D_OSI/data_post_processing/596154_Re3200_convergence.csv \
  --tolerance 1e-4 --output /tmp/BOX3D_OSI_convergence.png
```

### 同层通信重叠与计时

当前三维 [inputs](config/inputs) 显式设置 `lbm.osi_collide_comm_overlap=1`、
`performance.osi_detail_timing=0`。二维 inputs 未启用重叠、细分计时仍为 1；
程序未读到参数时，两个开关分别缺省为 0 和 1。
重叠仅用于有远端发送源且有剩余块的 OSI rank/level；设为 0 可用同一程序对照原顺序。
单 rank、A-B 或没有可重叠块的场景保持原顺序。

关闭细分计时保留 `comm/interp/average` 等主阶段计时，`osi_*` 为 0 表示未测量；
`perf(s)` 的 `osi_detail_timing` 字段给出实际开关。开启细分计时会同步 GPU、限制重叠，
只适合定位成本。重叠时 `comm` 区间包含剩余 GPU 碰撞，`collide` 包含该子集的提交时间；
两者不是独立 GPU 成本，不直接相加，端到端收益使用 `total`。

2026-10-22 的三轮配对在生产块大小128下，2/4 GPU真实AMR总耗时中位数下降5.52%/4.18%；
块大小32和单层没有稳定收益。对照两端均关闭细分计时，不能把该收益归因于默认计时开关变化。
[完整实验与数值边界](docs/ai_changes/2026-10-22-osi-collision-communication-overlap.md)限定于单节点host staging；
[较早的计时开关实验](docs/ai_changes/2026-10-15-osi-optional-detail-timing.md)只代表当时版本。

中心线、收敛和性能图已分别收敛为参数化入口；中心线绘图通过参数指定数据、步数、归一化、
参考数据和输出路径。ParaView 提取入口仍保留 Re=1000/3200、U_lid=0.05 和按运行目录命名输出的
专用约定。完整用法见[后处理入口索引](data_post_processing/README.md)。
收敛入口支持日志、已有CSV、受限格式PlotFile；日志仍按绝对step去重，后传入文件覆盖同一步，
缺少必需采样字段会报错。CSV/PlotFile没有正式状态时不推断已收敛，阈值必须显式给定。
该示例使用保留的历史CSV，不依赖已缺失的完整历史提交日志。

多个现役专项提交脚本共用 `scripts/osi_experiment_common.sh` 生成 hostfile 和
启动 MPI；带独立运行目录的实验还可用它记录完整命令。入口仍明确选择环境、
可执行文件、rank/GPU 数及覆盖参数。
锁步入口通过 `scripts/osi_lockstep_args.sh` 生成预设参数，保留各组步数、失败策略和
检查间隔。旧 direct 参数无需再写入新脚本；程序仍拒绝旧开关的 `=0` 请求。

### 锁步与 Re=1000 实验入口

在提交节点、算例目录中执行以下命令，入口会调用 `dsub` 提交自身，并选择对应的历史
作业名与日志名；作业内根据传入的预设调用现有 `scripts/submit.sh`。

| 命令 | 预设 |
|---|---|
| `./scripts/submit_osi_lockstep.sh 64` | 64 步，遇差异继续，逐步检查间隔 32 |
| `./scripts/submit_osi_lockstep.sh 128` | 128 步，遇差异停止，逐步检查间隔 32 |
| `./scripts/submit_osi_lockstep.sh 256` | 256 步，遇差异继续，逐步检查间隔 1 |
| `./scripts/submit_re1000_128k.sh ab` | Re=1000，128000 步，A-B 模式 |
| `./scripts/submit_re1000_128k.sh osi` | Re=1000，128000 步，OSI 模式 |

不带参数时分别选择 `64` 和 `osi`，`--help` 只打印用法。也可通过
`BOX3D_LOCKSTEP_STEPS` 或 `BOX3D_RE1000_MODE` 选择预设，位置参数优先。
提交时显式传递所选环境变量；`CCS_JOB_ID` 非空表示已在作业内，入口直接执行预设，
不再提交新作业。调度器资源声明与原入口一致：单节点、1 GPU、32 CPU、49152 MiB 内存。

锁步作业名为 `box3d_osi_lockstep_alllevels_<步数>`，标准输出与错误日志均为
`logs/submit/%J-osi-lockstep-alllevels-<步数>.log`；Re=1000 作业名为
`box3d_re1000_128k_<模式>`，日志为 `logs/submit/%J-re1000-128k-<模式>.log`。
主提交脚本继续在 `runs/<timestamp>_job<job-id>/` 保存实际输入、覆盖参数、命令和运行日志。

锁步预设按原行为整体设置 `AMREX_RUN_ARGS`；Re=1000 预设保留用户附加参数在前、
实验固定参数在后的覆盖顺序。需要复现历史实验时，仍须使用该实验的代码、可执行文件与
输入快照；这些入口只统一预设，不把当前输入当成历史输入。
五个旧入口的替换表和验证边界见[本次维护记录](docs/ai_changes/2026-10-19-merge-submit-presets.md)。

### 脚本入口索引

顶层 `scripts/` 保留以下 5 个用户入口；不要把内部支持脚本当作独立实验命令。

| 用户入口 | 输入与输出职责 |
|---|---|
| [compile.sh](scripts/compile.sh) | 使用算例构建配置，编译日志写入 `logs/compile/` |
| [submit_guarded.sh](scripts/submit_guarded.sh) | 推荐生产提交入口；启动主提交和提交节点监督，保存 `logs/submit/` 监督凭证 |
| [submit.sh](scripts/submit.sh) | 底层调度入口；冻结输入和覆盖参数，创建独立 `runs/` 目录，支持 checkpoint 续跑 |
| [submit_osi_lockstep.sh](scripts/submit_osi_lockstep.sh) | 选择 64/128/256 步锁步预设，输出同主提交 |
| [submit_re1000_128k.sh](scripts/submit_re1000_128k.sh) | 选择 Re=1000 的 A-B/OSI 128000 步预设，输出同主提交 |

内部依赖为 `guard_run.py`（作业内监督）、`watch_job.py`（调度监督）、`run_rank.sh`
（应用退出凭证）、`osi_experiment_common.sh`（MPI/hostfile/命令记录）与
`osi_lockstep_args.sh`（锁步参数）。它们保持原位置，仍由对应入口或测试调用。
算例级 `clean.sh` 转发入口已移除；本次没有执行清理程序或删除运行数据。

固定网格检查合并到 [tests/submit_osi_fixed_grid_check.sh](tests/submit_osi_fixed_grid_check.sh)：

```bash
./tests/submit_osi_fixed_grid_check.sh periodic
./tests/submit_osi_fixed_grid_check.sh nonperiodic
```

以上命令在提交节点执行，申请同原专项脚本的 2 GPU、2 rank，运行 64 步、level 0 的 seeded
OSI/A-B 检查。默认 `periodic`；也可设置 `BOX3D_FIXED_GRID_BOUNDARY`，位置参数优先。
保留原 `osi-stage3-mpi` / `osi-plan-nonperiodic` 作业与日志标识，附加 `AMREX_RUN_ARGS`
仍在固定参数之后。该入口的模拟参数检查通过，不代表当前硬件环境重新完成数值验收。

性能图入口移至 [data_post_processing/plot_run_performance.py](data_post_processing/plot_run_performance.py)，
从仓库根调用：

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/data_post_processing/plot_run_performance.py \
  <日志路径> --window-steps 1000 --output <图片路径>
```

它读取旧版完整 `perf(s)` / `perf_detail(s)` 记录；每窗步数默认 1000，须与运行配置一致，
相邻记录不连续或间隔不同会拒绝绘图。单条记录不能自动核实实际窗口长度。
输出标注来源、步数、每窗秒数及日志字段参照；缺失旧细分字段仍按兼容逻辑绘为 0，
不能解释为测得零成本，也未增加跨 rank 聚合机制。原解析和指标计算保持不变。
硬编码历史饼图见[后处理归档说明](data_post_processing/archive/README.md)，不作为当前性能工具。

完整逐文件处置和验证范围见[整体维护记录](docs/ai_changes/2026-10-19-script-directory-lifecycle.md)。

### 付费作业的退出保护

使用 `./scripts/submit_guarded.sh` 在提交节点启动独立监督器。`run.log` 直接写文件，
可用 `tail -f runs/<run-id>/run.log` 查看；不再通过异步 tee 转发完整日志。
每个 rank 返回后写 `rank_exit_<rank>.json`，启动器退出后写原子 `exit_status.json`。

- 应用打印 finalized 或所有 rank 返回后，启动器超过 60 秒仍不退出，回收该进程组，退出码 124。
- 提交节点读到退出凭证后，调度器超过 60 秒仍为 RUNNING，执行 `djob -T`，记录在
  `logs/submit/<job>-supervisor.json`。此类 FAILED 是异常收尾回收，须与应用退出码区分。
- 默认不设置总运行时限或调度硬时限。需要时可显式设置 `BOX3D_WALL_SECONDS`（0 表示不限）
  或提交参数 `-T`；平台自身的限制仍以平台配置为准。
- `BOX3D_EXIT_GRACE_SECONDS` 和 `BOX3D_SCHEDULER_EXIT_GRACE_SECONDS` 控制两层宽限期。

直接 `dsub -s scripts/submit.sh` 只有作业内监督，没有提交节点自动回收；
历史 runs 中复制的提交脚本也不会自动获得新保护。计算节点或提交节点故障、调度命令不可用时，
外部监督可能无法执行回收，不能承诺平台故障时绝不继续计费。
事故证据与验证见[退出收尾记录](docs/ai_changes/2026-10-16-job-exit-guard.md)。

## 当前配置与验证边界

当前三维 `config/inputs`：Re=3200，域长 1，基础网格 `128³`，最高 level=2，
blocking_factor=16、max_grid_size=128、grid_eff=0.7，regrid 间隔 32；
`stream_mode=1`、`collide_mode=1`、`interp_mode=0`；OSI 同层通信、插值搬运及接口平均统一使用 direct 路径；
device-direct 关闭，host-staging 分块为 2 MiB；最多 128000 步，PlotFile 间隔
3200、checkpoint 间隔 32000，收敛容差为 `1e-4`。这些是配置值，不表示长程已经完成。

三维使用 `state_error_6`，当前阈值映射仍取编译期最高层号 3，不能把 inputs 的
`lbm.err` 当作三维实际阈值；机制与覆盖比例定义见
[加密判据与建块](docs/osi_algorithm_and_architecture.md#判据打标建块与覆盖统计)。
BF32/BF16 的同起点 3200 步测量均已成功，但先前 BF16 的 32000 步续跑在首次
PlotFile 写入时失败；完整运行状态、覆盖结果和未解决事项见
[当前交接状态](docs/current_status.md#2026-10-21加密覆盖与续跑终态)。

网格尺寸、物理域和周期性由 inputs 设置；程序校验各向同性 spacing，并通过
`LbmGridParams` 传递派生参数。粗细覆盖掩码固定启用，`lbm.cf_mask_mode` 已移除。
普通粗到细插值缓存会检查父层 valid 模板覆盖，考虑周期像及物理边界延拓；
域内缺口立即报错，当前没有自动多层递归补缺。

正常 A-B/OSI 粗到细插值共用按布局缓存的批量缩放/插值任务；CUDA 界面平均合并各
interface Box 启动，与完整 valid 平均共用内核，并按编译期Q选择映射：Q27半warp/父单元、Q19单线程/父单元，
均为128线程/block；二维Q9保持整warp/父单元、256线程/block。
正式默认模型仍为三维Q27、二维Q9，不提供运行时Q19切换开关。
持久任务随既有粗细缓存重建/失效，执行时取得当前数组与 OSI 相位。共用数值核按布局选择 DDF 寻址，结果按布局搬运；
重网格临时插值与正常推进共用任务建表和执行器，但分别管理临时/持久生命周期。
完整 valid 与界面平均共用内核和启动器，分别保留全覆盖/界面范围；AB 完整平均还保留对 `f_new` 的缩放写入。
机制见[粗细层传输](docs/osi_algorithm_and_architecture.md#11-静态-coarse-fine-传输)，
正式程序与最新验收范围见[当前状态](docs/current_status.md)。

当前边界使用按层缓存的批量薄面任务，处理 Fab grown 区域内的非周期物理面，排除域外 ghost。
2026-10-15 作业 `613463` 完成三种插值、单/双 rank 动态 AMR 及固定周期网格
的六组 64 步前后对照；独立测试在 1/2 rank 各通过 432 组。数值范围及性能结果见
[最新边界交接状态](docs/current_status.md#2026-10-15-批量薄面边界收尾)。

2026-10-11 的冻结版本对照由作业 `611228` 完成：9 组 64 步动态 AMR/周期多 Fab
矩阵覆盖 1/2 ranks、插值模式 0/1/2 及三种 host-staging 策略；每步每层
uncovered DDF、各层 checkpoint valid/active DDF 的前后差均为 `linf=0`。
作业 `611229` 检查 CPC 缓存复用与重建；`611230` 检查 phase=0 checkpoint
重启相对连续运行的 valid DDF，均通过。完整条件、源码与可执行文件标识见
[架构去重验证记录](docs/ai_changes/2026-10-11-architecture-dedup-communication.md)。
动态算例的既有 physical-ghost source mismatch 仍在，不能称所有 ghost 全等。

长程严格逐值对照、多节点、多层周期动态 AMR、当前 HMPI/UCX 的 device-direct
以及运动 IBM 完整重启仍待对应验收。上述 phase=0 验证只覆盖该次 OSI DDF
重启，不外推为任意重启情形。历史性能、稳定运行或编译成功都不能单独证明数值正确性。
运动刚体状态未完整持久化，静态粒子容器的重启证据不能外推为完整 IBM restart。

## 文档导航

- [当前交接状态](docs/current_status.md)：验证版本、运行证据、历史结果与 pending 项。
- [OSI 算法与架构](docs/osi_algorithm_and_architecture.md)：地址、phase、边界、AMR 和 checkpoint 契约。
- [OSI 架构](docs/osi_algorithm_and_architecture.md#11-静态-coarse-fine-传输)：粗细层传输、缓存与覆盖门禁。
- [MPI 通信计划](docs/osi_mpi_communication_plan.md)：历史实验阶段与通信机制；现役粗细传输见 [OSI 架构](docs/osi_algorithm_and_architecture.md#11-静态-coarse-fine-传输)。
- [MLUPS 记录](docs/MLUPS记录.md)：历史性能及适用范围。
- [变更记录](docs/ai_changes/)：按提交保留的修改目的与验证摘要。

OSI 已移除 canonical 通信、插值搬运与接口平均写回回退。旧的
`osi_local_direct`、`osi_parallel_copy`、`osi_mpi_direct` 参数可暂时接受值 1，
值 0 会明确报错；新输入无需设置。插值工作区、完整 valid restriction、
A-B 参考态和 checkpoint/诊断需要的 canonical 数据仍保留。
