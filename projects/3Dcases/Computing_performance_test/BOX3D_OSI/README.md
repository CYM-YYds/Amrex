# BOX3D_OSI

基于 AMReX 26.06、C++20、CUDA 和 MPI 的三维 D3Q27 LBM 动态 AMR 算例。
本目录支持 canonical 双数组 A-B 基准和 one-step index（OSI）单数组推进；
相邻 `BOX3D` 是另一套基准算例。本文说明当前文件架构与使用入口，详细数值证据见
[当前交接状态](docs/current_status.md)，算法见 [OSI 架构](docs/osi_algorithm_and_architecture.md)。

更新时间：2026-10-09。

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
│   ├── D3Q19.H                 # 历史文件名；当前实际为 D3Q27，Q=27
│   ├── LbmGridParams.H         # 可传给 host/GPU 的运行时网格与物理参数
│   ├── LagrangeParticleContainer.H/.cpp # 粒子存储、插值和受力计算
│   ├── AuxiliaryPointContainer.H/.cpp  # 辅助测点容器
│   └── InitParticles.H         # 粒子初始化辅助函数
├── scripts/                    # 编译、生产提交和专项实验入口
├── tests/                      # 独立地址/覆盖测试及实验日志检查
├── docs/                       # 算法、状态、历史性能与 ai_changes 变更记录
├── data_post_processing/       # Python 后处理及历史图片
├── runs/                       # 独立实验目录：输入、命令、证据与输出
├── logs/                       # 编译和调度日志
├── tmp_build_dir/              # 构建中间文件
└── main3d.gnu.TPROF.MPI.CUDA.ex # 生产可执行文件
```

其他 `chk*`、`data*` 目录属于历史结果，不能当作待删除构建残留。
`.clangd`、`.clang-format` 和 `.clang-tidy` 是开发工具配置。
`config/Make.package` 决定实际参与构建的文件；源码目录并不自动全量编译。

## 源码职责与数据所有权

五个 `AmrCoreLBM*.cpp` 实现的是**同一个类**，共享 `AmrCoreLBM.H` 声明的成员；
它们不是五个独立管理器。头文件中的 `Box3dDetail` 保存共享计时器和 checkpoint
格式常量，原 `AmrCoreLBM_detail.H` 已合并并移除。

| 文件 | 负责的工作与代表入口 |
| --- | --- |
| [main.cpp](src/main.cpp) | 读取启动配置、构造 Geometry/AmrInfo/驱动对象；初始化或重启；安排 regrid、`Cycle2`、输出与停止条件 |
| [AmrCoreLBM.H](src/AmrCoreLBM.H) | 声明 RuntimeParams、PerfStats、RunMode/DdfLayout；拥有 DDF、phase、宏观场、mask、粗细层与通信缓存 |
| [AmrCoreLBM.cpp](src/AmrCoreLBM.cpp) | 构造与参数读取、`ComputeMacroLevel`、PlotFile、`WriteCheckpoint`/`ReadCheckpoint`、粒子接口 |
| [AmrCoreLBM_amr.cpp](src/AmrCoreLBM_amr.cpp) | `MakeNewLevel*`/`RemakeLevel`/`ClearLevel`/`ErrorEst` 等 AMReX 回调；`RefineMesh`、覆盖 mask、插值/平均缓存、模板覆盖检查 |
| [AmrCoreLBM_advance.cpp](src/AmrCoreLBM_advance.cpp) | `AdvanceLevel`、Collide/Stream/Boundary/Swap；`FillGhostLevel`、`CommunicateLevel` 等布局分派；界面/完整平均及平均后物理边界修复；涡量与剪切 |
| [AmrCoreLBM_osi.cpp](src/AmrCoreLBM_osi.cpp) | `DecodeOsiValid`；`FillOsiGhostFromCoarse`、`AverageDownOsi*`；`FillBoundaryOsi` 分派后的通信实现、通信缓存与 `ParallelCopyOsi` 重网格迁移 |
| [AmrCoreLBM_diagnostics.cpp](src/AmrCoreLBM_diagnostics.cpp) | 阶段级/逐 cell 对照、Stream 源诊断、checksum、checkpoint 对比和收敛测量；不是生产推进入口 |
| [OsiAbVerification.H/.cpp](src/OsiAbVerification.H) | 按 `verification.*` 配置安排 regrid 前后、首次 FillGhost 和推进后的检查；实际比较由驱动类诊断函数执行 |
| [OsiIndex.H](src/OsiIndex.H) | `FabGeometry`、`PhaseShift3`、`MakeOsiFabContext(ring, phase)` 和 `osi_address`；只计算索引，不拥有 level 状态 |
| [OsiCommunication.H](src/OsiCommunication.H) | local/remote copy、pack/unpack tag 和 MPI 交换公共骨架；具体区域与布局选择由 `_osi.cpp` 决定 |
| [Kernels.H](src/Kernels.H) | 单元级数值计算；由驱动层传入 Array4、几何、相位和物理参数并发射 kernel |
| [D3Q19.H](src/D3Q19.H) / [LbmGridParams.H](src/LbmGridParams.H) | 前者给出速度集、权重、Q、最大编译层级等模型常量；后者传递运行时尺寸、dx/dt 与黏度 |

A-B 状态是每层 `f_old/f_new`；OSI 生产状态是每层 `osi_state` 和 `osi_phase`。
开启 `verification.osi_ab_check` 时额外分配 A-B 状态用于锁步对照。
`ring` 必须来自完整 grown-Fab，不能用 tilebox、碰撞域或物理域裁剪后的 Box 替代。
相位属于 level 驱动，在 host 上准备 Fab 上下文，再供整个 kernel 复用。

## 实际推进调用链

`main.cpp` 的现役入口是 `Cycle2`。每个 coarse step 先处理需要的 regrid，
然后进入多层递归；同文件中的 Rohde/Jaber 等其他周期函数不在当前主调用路径上。

```text
main coarse-step 循环
  ├─ regrid 步：AverageDownValid → RepairCurrentStatePhysicalBoundary → RefineMesh
  └─ Cycle2(lev, time)
       ├─ 有细层时：FillGhostLevel(lev, time, scale) 填 lev+1 的 ghost
       ├─ AdvanceLevel(lev)
       │    ├─ Collide → CommunicateLevel → Stream → Boundary
       │    └─ SwapLevel
       └─ 有细层时：Cycle2(lev+1) 两次 → AverageDownInterfaceLevel(lev+1)
```

两个 fine substeps 共用父层在本次调用开始时的 ghost 填充。
普通推进只执行所选布局；锁步验证模式在 `AdvanceLevel` 内显式执行两种布局，
在碰撞、通信、迁移、边界等阶段立即比较。
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
dsub -s ./scripts/submit.sh
```

`compile.sh` 转发到仓库级编译脚本，在编译节点加载环境；构建采用 AMReX 26.06、
MPI、CUDA、TinyProfiler，项目要求 C++20 和 GCC 11+。
主提交脚本创建 `runs/<timestamp>_job<job-id>/`，冻结 `inputs`、`overrides.txt`、
`command.txt`、`manifest.txt`，并保存 `run.log`；相对 PlotFile/checkpoint 路径落在该目录。
它记录可执行文件 SHA256，但程序使用指向算例可执行文件的符号链接；不能把该流程
描述为冻结了独立可执行副本或完整源码快照。严格版本对比应另存 app 与源码证据。

修改生产配置使用 [config/inputs](config/inputs)。专项测试可通过 `AMREX_RUN_ARGS`
覆盖参数，主提交脚本会记录原始字符串。`scripts/submit_*.sh` 的行为需逐脚本查看：
有些包装主提交脚本，有些自建实验目录和 app 快照，不能一概而论。
`submit_osi_stage2_smoke.sh`、`submit_osi_stage3_single.sh`、
`submit_osi_stage3_single_array.sh`、`submit_osi_stage7_restart.sh` 仍引用缺失的
`config/inputs_osi`，属于历史入口，不能直接作为当前验收命令。

续跑时给主提交脚本设置 `RESTART_CHECKPOINT` 为 checkpoint 完整路径；
脚本在新运行目录创建 `chk<step>` 源链接并设置 begin_step，后续 checkpoint 写在新目录。
OSI checkpoint 保存 canonical DDF，读取后 phase 从 0 开始。

Python 后处理统一使用仓库根 `.venv/bin/python`。例如从仓库根运行：

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/data_post_processing/plot_586660_convergence.py
```

该示例处理历史作业，输入路径以脚本为准；图表应注明数据、步数、归一化和参考来源。

## 当前配置与验证边界

当前 `config/inputs`：Re=1000，基础网格 `128³`，最高 level=2，regrid 间隔 32；
`stream_mode=1`、`collide_mode=1`、`interp_mode=0`；OSI 同层通信、插值搬运及接口平均统一使用 direct 路径；
device-direct 关闭，host-staging 分块为 2 MiB；最多 128000 步，PlotFile 间隔
3200、checkpoint 间隔 32000，收敛容差为 `1e-4`。这些是配置值，不表示长程已经完成。

网格尺寸、物理域和周期性由 inputs 设置；程序校验各向同性 spacing，并通过
`LbmGridParams` 传递派生参数。粗细覆盖掩码固定启用，`lbm.cf_mask_mode` 已移除。
普通粗到细插值缓存会检查父层 valid 模板覆盖，考虑周期像及物理边界延拓；
域内缺口立即报错，当前没有自动多层递归补缺。

2026-10-09 的 Fab 上下文重构作业 `610666` 在 2 ranks/2 GPUs 上完成 A-B、direct、
fallback 和周期域四组 64 步检查；direct/fallback 动态 regrid 到 level 2，阶段级
检查均为 `linf=0`，独立 A-B checkpoint 的全局 DDF 差约 `1.17e-15`。
非周期 physical-ghost source mismatch 仍存在，valid/internal-ghost mismatch 为零。
这是该作业冻结版本的有限窗口证据；后续上下文函数合并及头文件合并的编译证据
与运行证据分别记录在 [当前交接状态](docs/current_status.md)。

长程严格逐值对照、多节点、多层周期动态 AMR、重启和当前 HMPI/UCX 的 device-direct
仍待对应验收。历史性能、稳定运行或编译成功都不能单独证明数值正确性。
运动刚体状态未完整持久化，静态粒子容器的重启证据不能外推为完整 IBM restart。

## 文档导航

- [当前交接状态](docs/current_status.md)：验证版本、运行证据、历史结果与 pending 项。
- [OSI 算法与架构](docs/osi_algorithm_and_architecture.md)：地址、phase、边界、AMR 和 checkpoint 契约。
- [DDF 粗细网格填充](docs/DDF粗细网格填充学习文档.md)：模板来源、缓存与覆盖门禁。
- [MPI 通信计划](docs/osi_mpi_communication_plan.md)与 [ParallelCopy 计划](docs/osi_parallelcopy_optimization_plan.md)：机制、实验阶段和未决事项。
- [性能分析](docs/osi_performance_profiling.md)与 [MLUPS 记录](docs/MLUPS记录.md)：历史性能及适用范围。
- [变更记录](docs/ai_changes/)：按提交保留的修改目的与验证摘要。

OSI 已移除 canonical 通信、插值搬运与接口平均写回回退。旧的
`osi_local_direct`、`osi_parallel_copy`、`osi_mpi_direct` 参数可暂时接受值 1，
值 0 会明确报错；新输入无需设置。插值工作区、完整 valid restriction、
A-B 参考态和 checkpoint/诊断需要的 canonical 数据仍保留。

本次删除通过作业 `610808` 的单/双 rank 动态 AMR 与周期域 64 步检查，
阶段比较均为 `linf=0`；验证细节及边界见
[变更记录](docs/ai_changes/2026-10-09-remove-canonical-fallbacks.md)。
