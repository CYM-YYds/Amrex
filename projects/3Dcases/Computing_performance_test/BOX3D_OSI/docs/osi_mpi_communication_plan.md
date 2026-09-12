# OSI 跨 MPI 通信实施计划

更新时间：2026-09-12

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
`osi_mpi_pack`、`osi_mpi_wait` 和 `osi_mpi_unpack`；buffer 仍在每次调用时分配，属于
正确性版本，不是最终性能实现。

### 阶段 C：CUDA-aware MPI 优化

在阶段 B 数值通过后，增加 device buffer transport：

```text
GPU raw state → device pack buffer → CUDA-aware MPI → device unpack buffer → GPU raw state
```

需要确认目标 HMPI 的 CUDA-aware 支持、MPI stream 同步语义和 device pointer 传递方式。
若运行环境不稳定，保留阶段 B 作为 correctness fallback。

### 阶段 D：混合路径与生产切换

最终路径按通信类型选择：

```text
同 rank       → rank-local raw-to-raw copy
跨 rank       → OSI-aware pack/unpack
coarse-fine   → 现有 AMR 专用传输
物理边界     → 现有 Boundary kernel
```

原 `CommunicateOsiLevel()` 始终保留，并由运行时开关控制 fallback，便于逐作业 A/B。

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
