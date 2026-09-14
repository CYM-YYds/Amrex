# OSI 跨 MPI 通信实施计划

更新时间：2026-09-14

本文只描述 `BOX3D_OSI` 的跨 MPI OSI-aware same-level 通信，不改变现有
`CommunicateOsiLevel()` 的行为。当前 rank-local direct copy 已单独实现；跨 MPI 的
host-staging correctness 路径也已接入，但默认仍回退 canonical 路径。

运行时开关为：

- `lbm.osi_local_direct=1`：启用同 rank raw-to-raw copy；
- `lbm.osi_mpi_direct=1`：在多 rank 下进一步启用 OSI-aware pack/unpack；
- `lbm.osi_mpi_direct=0`：保留原 `CommunicateOsiLevel()` 作为 fallback。

## 1. 当前状态与目标

当前多 rank 路径为：

```text
OSI raw state
  -> Decode 到 canonical sync buffer
  -> canonical.FillBoundary()
  -> Encode 回 OSI raw state
```

目标路径为：

```text
OSI raw source
  -> OSI-aware GPU pack
  -> MPI message
  -> OSI-aware GPU unpack
  -> OSI raw destination ghost
```

目标是消除跨 MPI 通信中的 canonical Decode/Encode 和中间 DDF 缓冲，同时保持
AMReX `FillBoundary` 的 source/destination 逻辑语义、周期映射和消息拓扑。

## 2. 不能直接复用普通 FillBoundary 的原因

AMReX 的 `CopyComTag` 只描述普通逻辑 Box 和 component 范围。OSI 中同一个 logical
cell 的 raw 地址同时依赖：

- source 或 destination Fab 的 `smallEnd/length`；
- 当前 level 的 phase；
- 离散速度方向 `q`；
- 周期边界产生的 logical shift。

因此不能把 raw OSI Box 直接交给普通 `FillBoundary` 或 `ParallelCopy`。必须保留普通
通信拓扑，再在 pack/unpack 阶段执行 source/destination 地址投影。

## 3. 实施阶段

### 阶段 A：冻结并提取通信计划

1. 从 `BoxArray`、`DistributionMapping` 和 `Periodicity` 构造与 AMReX
   `FillBoundary` 等价的 logical source/destination 配对。
2. 每个记录至少包含：source Fab、destination Fab、source/destination logical Box、
   peer rank、周期 shift 和有效 cell 数。
3. source 与 destination 必须成对保存，不能像旧的 decode/encode cache 那样独立
   `removeOverlap()` 后丢失配对关系。
4. 对 source/destination raw 映射可能产生的环首/环尾进行构建期 Box 拆分；每个轴
   最多切成两段，三维最多 8 个矩形片段。
5. 在 `define`、`regrid`、`ClearLevel` 后销毁并重建计划。

### 阶段 B：正确性优先的 MPI transport

先实现与 CUDA-aware MPI 无关的版本：

1. GPU kernel 按 logical tag 和 `q` 将 OSI raw source 打包到 pinned host buffer；
2. 使用 `MPI_Irecv`/`MPI_Isend` 或 AMReX `ParallelDescriptor` 完成非阻塞交换；
3. 等待通信完成后，GPU kernel 将 pinned host buffer 解包到 destination raw ghost；
4. 每个 peer 只发送一个聚合消息，消息内部按固定 tag 顺序排列；
5. 所有 rank 根据同一份全局 BoxArray/DistributionMapping 推导发送和接收顺序，避免
   额外的动态元数据交换。

该版本的目的只是验证地址、消息顺序和 ghost 结果，不预设一定比 canonical 路径更快。

当前实现为每个 peer 聚合一条消息，GPU pack 后复制到 pinned host buffer，通过
`ParallelDescriptor::Arecv/Asend` 交换，再复制回 device 并由 GPU unpack。已分别记录
`osi_mpi_pack`、`osi_mpi_wait` 和 `osi_mpi_unpack`。通信计划、peer 偏移和 staging buffer
随网格缓存一次性建立；每步 pack/unpack 分别用一个 TagVector 融合 GPU kernel，避免
逐 tag 启动和反复分配。

### 阶段 C：CUDA-aware MPI 优化

在阶段 B 数值通过后，增加 device buffer transport：

```text
GPU raw state → device pack buffer → CUDA-aware MPI → device unpack buffer → GPU raw state
```

需要确认目标 HMPI 的 CUDA-aware 支持、MPI stream 同步语义和 device pointer 传递方式。
若运行环境不稳定，保留阶段 B 作为 correctness fallback。

代码已增加 `lbm.osi_mpi_device_direct` 实验开关，并要求 AMReX
`UseGpuAwareMpi()` 返回真。当前 HMPI/UCX 在 job `591185` 中对 device pointer 仍走
`process_vm_readv`，以 `Bad address` 失败；当前环境不得通过
`amrex.use_gpu_aware_mpi=1` 强制绕过能力检测。改用 CUDA-aware OpenMPI 4.1.5 和
UCX 1.12.1 后，job `595584` 的 device-buffer 正确性通过；但 job `595585` 的
device MPI wait 约 44.85 s，host-overlap 仅约 1.77--2.06 s。阶段 C 因此是
“正确性通过、性能不通过”，默认仍保留阶段 B。

### 阶段 D：混合路径与生产切换

最终路径按通信类型选择：

```text
同 rank       → rank-local raw-to-raw copy
跨 rank       → OSI-aware pack/unpack
coarse-fine   → 现有 AMR 专用传输
物理边界     → 现有 Boundary kernel
```

原 `CommunicateOsiLevel()` 始终保留，并由运行时开关控制 fallback，便于逐作业 A/B。

### 阶段 E：复用 AMReX FillBoundary 通信计划（已实现并完成首轮验收）

当前 direct 路径已经与 `FillBoundary()` 语义等价，但通信计划仍由算例重复枚举
`BoxArray`、周期像和 ghost 交集。下一轮优化直接复用 AMReX 为同一个 `MultiFab`
生成并缓存的 `FB` 元数据：

1. 通过 `FabArrayBase::getFB()` 获取 `m_LocTags`、`m_SndTags` 和 `m_RcvTags`；
2. 同 rank copy 从 `m_LocTags` 构造 OSI raw-to-raw tag；
3. 跨 rank pack/unpack 分别按 `m_SndTags`/`m_RcvTags` 的 peer 分组与既定排序构造，
   不再由算例独立推导消息顺序；
4. OSI 只保留 phase、方向 `q` 和 Fab geometry 对 logical 坐标的 raw 地址投影；
5. 继续保留现有 fallback、host-staging/device-buffer 开关和分项计时，先完成逐阶段
   数值回归，再进行与 job `591187` 相同配置的性能验收。

第一步只替换计划来源，不同时改变 kernel、MPI transport 或缓冲布局。这样可以把
性能变化归因于计划一致性，并避免把多个优化变量混在同一次验收中。后续若 profile
仍显示可观的 CPU/MPI 调度成本，再评估复用 AMReX `FillBoundary_nowait/finish` 的
请求生命周期；该步骤必须保留 OSI 自定义 pack/unpack，不能直接调用普通 component
copy kernel。

2026-09-14 首轮实施与验收记录：

- direct 计划来源已替换为 `getFB()` 的 `m_LocTags/m_SndTags/m_RcvTags`，未修改共享
  AMReX，也未改变 MPI transport、缓冲布局或 OSI pack/unpack kernel；
- CUDA + MPI 编译日志 `compile-20260914T152040.log` 成功；
- 周期正确性 job `595613`：2 ranks、2 GPUs、8 Fab、64 步，Initial、Collision、
  Communication、Stream、Boundary、Swap 共 384 次逐点检查全部 `linf=0`；
- 非周期正确性 job `595921`：相同 rank/GPU/Fab/步数，将六面改为非周期；384 次
  逐点检查全部 `linf=0`，本 rank `remote_records` 由周期配置的 144 降为 32；
- 性能 jobs `595615`/`595617`：同一作业内先运行 A-B、再运行 OSI direct，各 1000 步。
  A-B rank 时间分别为 4.597--4.606 s 和 4.623--4.629 s；OSI 分别为
  4.354--4.360 s 和 4.370--4.376 s。按对应作业/rank 计算，OSI 快约
  5.28%--5.47%，此前 job `591187` 中慢 6.1%--6.5% 的差距已逆转；
- 当前日志中的 `remote_records` 是本 rank 的 send 与 recv tag 记录总数，不再是旧手工
  全局枚举的 `remote_pairs`，两者不能直接按数值比较。

上述性能结论只覆盖单节点双 GPU、单层全周期固定网格；非周期仅完成正确性回归，
多节点和动态 AMR 仍需分别验收，不能由这两次性能作业外推。

## 4. 正确性不变量

- 通信使用当前 phase；不能使用下一次 Stream 提交后的 phase。
- source 和 destination 各自使用所属 Fab 的 geometry。
- 周期 shift 只改变 logical source/destination 配对，不改变 raw phase 定义。
- 非周期域外 ghost 不作为同层 MPI source；其值由物理边界流程负责。
- 第一版保持 `nGrow=2`、27 个 q 和现有 Collision/Stream cell 集合不变。
- 同一 destination ghost 若有多个候选 source，选择规则必须与 AMReX 原
  `FillBoundary` 一致。
- regrid、BoxArray 或 DistributionMapping 变化后，旧 tag、buffer 和 MPI request
  不得继续使用。

## 5. 验收矩阵

| 阶段 | 配置 | 主要检查 |
|---|---|---|
| A | CPU/小 BoxArray | logical tag 数量、周期 shift、Box 拆分 |
| B1 | 单层、2 Fab、2 rank、周期 | 通信后逐 cell/逐 q 一致 |
| B2 | 单层、多 Fab、2 rank、非周期 | 排除域外 ghost 后 oracle 一致 |
| B3 | level 0--1、2 rank、无 regrid | per-level phase 与 coarse-fine 隔离 |
| B4 | level 0--1、2 rank、至少一次 regrid | tag 重建和 restart 前后状态 |
| C | 与 B2/B3 相同 | device buffer 与 host buffer 逐点一致 |
| D | 1000 步生产配置 | comm、solver、total、MLUPS 受控 A/B |

阶段 B/C 的阶段 oracle 至少检查 Initial、Collision、Communication、Stream、Boundary、
Swap 六个阶段；性能实验必须同时记录 MPI rank、GPU、BoxArray、active cells、源码
commit 和输入覆盖参数。

## 6. 计时与风险

单列以下计时：

- `osi_mpi_pack`；
- `osi_mpi_wait`；
- `osi_mpi_unpack`；
- MPI message count/bytes；
- rank-local direct copy；
- fallback canonical Decode/FillBoundary/Encode。

主要风险：

1. raw Box 环绕拆分错误导致少量方向 ghost 错位；
2. 多个周期像或重叠 Fab 造成重复写入；
3. MPI 消息顺序或大小不一致导致死锁；
4. host staging 的拷贝成本抵消通信收益；
5. CUDA-aware MPI 对 stream/同步的实现差异；
6. regrid 后旧 Array4 指针或旧 phase 被 tag 继续引用。

在阶段 B 数值验收完成前，不得关闭原 `CommunicateOsiLevel()` fallback，也不能将
单 rank direct copy 的性能结果外推到多 rank。

## 7. 2026-09-12 验证记录

- 编译日志 `compile-20260912T093450.log`：CUDA + MPI 构建和链接成功；
- B1 job `591167`：2 ranks、2 GPUs、单层、8 Fab、全周期、64 步；六个阶段各
  64 次检查全部 `linf=0`，周期 remote pair 数为 176；
- B2 job `591170`：同样的两 rank/8 Fab 配置，改为六面非周期；六个阶段各
  64 次检查全部 `linf=0`，remote pair 数降为 44；
- B3 尚未验收：强制 level 0/1 全覆盖层级后，新路径 job `591172` 与 canonical
  fallback job `591173` 都在第 1 步 `Communication` 后保持 `linf=0`，随后在
  level 0 `Stream` 阶段以相同位置和相同 `linf=7.037037037037064e-4` 失败。
  因为 fallback 能完全复现，该差异不能归因于本次 MPI pack/unpack；后续应使用
  uncovered/active 范围的多层 oracle，或构造非全覆盖的静态 level 1 后再验收 B3。

因此当前结论只覆盖单层、两 rank、多 Fab 的周期与非周期 same-level 通信；尚不宣称
多层 AMR、regrid、restart 或 CUDA-aware MPI 已通过。

## 8. 2026-09-12 缓存与融合优化记录

- CUDA + MPI 构建日志 `compile-20260912T121424.log` 成功；
- correctness job `591180`：2 ranks、2 GPUs、单层、8 Fab、全周期、64 步，六个阶段
  全部 `linf=0`；
- performance job `591181`：同一作业内 AB 为 4.981--4.989 s、420--421 MLUPS，
  OSI MPI direct 为 5.505--5.517 s、380--381 MLUPS；OSI 总耗时差距约 10.8%；
- 上一实现的同作业 `591174` 中，OSI 相对 AB 慢约 31.7%。本次优化把同作业内差距
  缩小约三分之二，但 OSI 仍未反超；剩余热点主要是 host staging 和 MPI wait。
- overlap correctness job `591186`：双 GPU 64 步六阶段全部 `linf=0`；performance
  job `591187`：OSI 相对 A-B 的总耗时差距进一步降至约 6.1%--6.5%。
- device job `591185`：当前 HMPI/UCX 对 device pointer 报 `process_vm_readv: Bad address`；
  只证明当前 transport 不可用，不构成 OSI 数值失败。
- capability-gate job `591188`：未伪造能力标志时，程序在进入时间推进前按预期拒绝
  device transport，没有把不支持的指针交给 MPI。
- CUDA-aware jobs `595584`/`595585`：device-buffer 六阶段正确性全部 `linf=0`；但
  device-overlap solver 约 45.90 s，host-overlap 约 4.13 s，性能验收不通过。
