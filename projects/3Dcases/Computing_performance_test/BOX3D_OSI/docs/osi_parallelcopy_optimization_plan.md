# BOX3D_OSI 的 OSI ParallelCopy 优化计划

## 目标

在 `BOX3D_OSI` 内增加与 OSI raw state/phase 语义一致的 `OsiParallelCopy` 路径，
让插值和平均阶段不再经历 `OSI raw -> canonical MultiFab -> AMReX ParallelCopy`
的中转。单 GPU、本地多 Fab、跨 MPI rank 和 GPU-aware MPI 均按阶段启用，保留旧路径
作为运行时回退。最终使用相同 checkpoint、网格、regrid、步数和硬件，对 A-B 与 OSI
进行分项计时和逐 cell/逐 q 正确性比较。

“耗时接近”不是无条件保证，而是受控实验的验收目标：在同一硬件和输入下，OSI 的
插值/平均核心时间与 A-B 分别达到单 GPU `<= 1.25x`、同节点多 GPU `<= 1.50x`；
若通信或硬件差异导致无法满足，必须保留分项证据并明确瓶颈，不以总 MLUPS 掩盖中转成本。

## 可观察完成标准

### REQ-1：计划、开关和可回退性

- 本文档记录接口、阶段、所有权、门槛和验证命令。
- 新路径有显式运行时开关，默认值不改变现有生产行为；关闭开关时字节级复用旧路径。
- 任何不支持的 phase、Fab layout、MPI/GPU 组合均可回退并产生一次明确诊断。
- 证据：计划文档、配置说明、`git diff`、开关两态的编译和小规模运行日志。

### REQ-2：单 rank OSI raw-to-raw 本地复制

- 复用 `FabArrayBase::CPC` 的 `m_LocTags` 拓扑，在 source/destination phase 和各自
  FabGeometry 下计算 raw 地址，直接执行 OSI raw copy。
- `FillOsiGhostFromCoarse` 的本地路径不创建 canonical 临时 `MultiFab`。
- 证据：实现 diff、单 rank 逐 cell/逐 q 对照，`Linf` 为 0 或在明确浮点容差内，
  以及 local-copy 计时字段。

### REQ-3：跨 rank OSI pack/unpack

- 复用 `m_SndTags/m_RcvTags`，从 OSI raw source 直接 pack 到已有通信 buffer，收包后
  直接 unpack 到 OSI raw destination；不得先 decode 到 canonical temporary。
- 同时支持现有 host-staging 和 GPU-aware/device-direct 两种 MPI 模式；无法使用
  device-direct 时仍只保留必要的连续通信 buffer。
- 证据：2 rank 周期边界和非周期边界运行；pack/unpack、D2H/H2D、MPI wait 分项日志；
  与 canonical 参考逐 cell/逐 q 比较。

### REQ-4：插值与平均阶段接入

- `FillOsiGhostFromCoarse`、`AverageDownInterfaceLevel` 首先接入新路径；旧路径保持可选。
- `RemakeDdfState` 等 state 重建路径仅在地址/phase 语义已验证后接入，不能以普通
  `ParallelCopy` 传输 twisted raw state。
- 证据：阶段计时包含 `osi_parallel_copy_local/pack/wait/unpack`，并能定位每个调用点。

### REQ-5：A-B/OSI 正确性门槛

- 相同输入、相同随机/边界条件、相同 AMR 时间线下，插值目标和平均目标逐 cell、逐 q
  比较；先单 rank，再 2 rank/2 GPU，再六面非周期边界。
- 任何非零差异必须给出首次差异 step、level、Fab、cell、q 和 phase；不能以 checksum
  或范围相同替代点对点证据。
- 证据：机器可读比较文件和运行日志，明确绝对/相对容差。

### REQ-6：性能验收

- 固定 checkpoint、网格、`regrid_int`、步数和 GPU 映射，分别测试单 GPU、同节点 2 GPU。
- 至少报告插值、平均、local copy、MPI pack、D2H、MPI wait、H2D、unpack 和总阶段时间；
  与 A-B 使用相同计时边界。
- 目标：OSI/A-B 插值和平均核心时间单 GPU `<=1.25`，同节点多 GPU `<=1.50`；同时给出
  596890/596891 仅作为历史背景，不能代替新候选证据。
- 证据：CSV/JSONL 计时 artifact、命令行、候选源码 commit/diff identity。

## 任务图与所有权

| 阶段 | 任务 | 需求 | 所有者与写入范围 |
|---|---|---|---|
| 0 | 建立基线、开关和计时字段 | REQ-1, REQ-6 | 主控；`AmrCoreLBM.H/.cpp`、`config/inputs`、本文档 |
| 1 | 设计并实现 CPC tag 到 OSI raw 的本地复制 | REQ-2 | 主控；`OsiCommunication.H` 及其唯一实现文件 |
| 2 | 接入 `FillOsiGhostFromCoarse`，保留 fallback | REQ-2, REQ-4 | 主控；`AmrCoreLBM.cpp` |
| 3 | 实现跨 rank pack/unpack 和通信模式选择 | REQ-3 | 主控；OSI 通信实现和 `AmrCoreLBM.H` |
| 4 | 接入平均路径及必要的 state 重建路径 | REQ-4 | 主控；`AmrCoreLBM.cpp` |
| 5 | 编译、单 rank/多 rank 正确性验证 | REQ-5 | 主控；只生成受控日志/artifact，不改源码 |
| 6 | 性能 A/B 对照、审查和回退决定 | REQ-1, REQ-6 | 主控；只生成受控日志/artifact |

本轮 worker 通道不可用，因此采用 Compatibility 模式且不再重复派发同一任务；所有
源码文件由主控统一写入，避免接口所有权冲突。

## 实现分阶段方案

1. **接口与数据结构**：新增 `OsiParallelCopy` 所需的 source/destination phase、
   `FabArrayBase::CPC` 引用、raw geometry 映射和计时结果；不修改 AMReX 原生
   `MultiFab::ParallelCopy`。
2. **单 rank/local**：先只消费 `m_LocTags`，验证 source/destination Fab 的坐标、q
   顺序和 phase。禁止跨 rank 代码在此阶段静默介入。
3. **MPI**：消费 `m_SndTags/m_RcvTags`，直接 raw pack/unpack；复用已有 tag cache、
   buffer、host-staging 和 device-direct 开关。MPI buffer 仍是必要的连续传输介质，
   消除的是 canonical 临时对象和重复 decode/encode。
4. **调用点**：优先替换 `FillOsiGhostFromCoarse` 和 `AverageDownInterfaceLevel`；
   `RemakeDdfState` 在独立 phase/address 测试通过后再启用。
5. **回退**：所有阶段都以 `lbm.osi_parallel_copy=0/1` 控制；新路径遇到不支持的 layout
   或 phase 时记录原因并回到旧实现，不改变数值结果。

## 正确性与性能验证

编译遵循算例 `config/GNUmakefile` 和脚本，运行使用固定输入及 checkpoint。Python 后处理
统一使用仓库根 `.venv/bin/python`。每次候选源码改变后，旧 evidence 全部失效并重跑受影响
检查。验证 artifact 至少包括：

- `osi_parallel_copy_correctness.jsonl`：case、rank、level、Fab、cell、q、phase、差异和容差；
- `osi_parallel_copy_timing.csv`：调用点和各阶段纳秒/毫秒计时；
- 编译命令、运行命令、源码 commit、输入文件 hash、GPU/MPI 映射。

通过条件是所有 REQ 均有绑定到最终源码的 diff、命令输出或 artifact；只有“编译成功”、
“job 完成”、checksum 相同或总 MLUPS 变好均不足以单独判定通过。

## 当前阶段状态（2026-09-22）

- 已完成：单 rank 插值路径的 raw-to-canonical 直接复制（`FillOsiGhostFromCoarse`）。
- 已完成：单 rank 平均接口直接写入 coarse OSI raw；多 rank 平均通过 CPC tags
  执行 canonical pack、MPI、按 phase 解包到 raw；开关关闭时保留原有回退。
- 已验证：候选版本通过 `BOX3D_OSI/scripts/compile.sh` 的 CUDA/MPI 编译。
- 已验证：2 ranks/2 GPUs、固定布局 host-staging 的 active-cell 54 个 `(level,q)`
  `Linf=0`，插值与平均 8 步短窗口接近 A-B；见 `current_status.md` 和
  `logs/validation/osi_parallelcopy_{checkpoint,perf}_602116.jsonl`。
- 未完成：dynamic regrid 的逐阶段严格比较、多节点性能和平均 device-direct 的
  运行验收。后者在 HMPI/UCX 作业 `602119` 中于普通通信阶段先行失败，不能用
  编译成功或已有 host-staging 结果替代。
- 当前 `lbm.osi_parallel_copy` 默认仍为 `0`；device-direct 仅显式 opt-in。

## GPU 冒烟与阶段性性能证据（2026-09-21）

- 作业 `601982`：单 GPU、2 步，确认编译产物能在 GPU 节点完成 CUDA 初始化和计算；
  未触发 AMR 插值/平均。
- 作业 `601983`：单 GPU、6 步、`lbm.err=0`，触发全域细化；用于确认 level 1
  生命周期，但全覆盖布局没有形成接口 Box。
- 作业 `601984`：单 GPU、36 步、原始误差阈值，形成 level 1 约 12.5% 的部分细化；
  在 kernel 合并前记录 direct 路径基线。
- 作业 `601985`：与 `601984` 相同输入的 A-B 对照（`stream_mode=0`）。
- 作业 `601991`：同一候选源码在合并 Q 分量 kernel 后的 OSI 结果。`run.log` 中
  step 32 的插值/平均为 `0.314/0.274 ms`，A-B 为 `0.267/0.266 ms`；step 36
  为 `0.706/0.739 ms`，A-B 为 `0.727/0.800 ms`。该结果只支持当前单 GPU、短窗口
  的阶段性性能判断，不替代逐 cell/逐 q 正确性和多 GPU验收。
