# OSI 整块碰撞与同层通信重叠

## 目的与实现

针对 `FillBoundaryOsi` 前必须等待全部碰撞的串行关系，按远端发送源 Fab 分类，
将剩余整块碰撞与 host staging/MPI 重叠。基线为 `53de250`，包含此前的通信缓冲布局优化。

- 从既有 FB 计划的 `m_SndTags.srcIndex` 派生 `send_fabs`；只标记远端发送源，
  不将接收块或本地复制块另建互斥分类。分类与 `FillBoundaryCache` 一起重建、清除。
- 先碰撞发送源块，完成 pack 并同步后，通过 `FillBoundaryOsi` 内一次调用范围的回调
  提交剩余碰撞。复用原碰撞 kernel、通信 tags、缓冲和完成等待，不增加跨步骤 MPI 状态。
- 剩余碰撞使用 stream 0，关闭该子集的 MFIter 隐式同步和函数末同步，让 CPU 继续推进
  stream 1 的 staging 与 MPI。本地 seam copy 和 unpack 在 stream 0 上排在剩余碰撞之后，
  防止提前覆盖其 ghost 输入；phase 提交仍在通信完成之后。
- 两个碰撞子集互斥且并集不变，仍使用原 grown 区域、物理域裁剪及 covered/interface 判断。
  每层 calls 仍按一次推进统计，launch_cells 分两部分累加；没有减少碰撞工作量。
- 新开关 `lbm.osi_collide_comm_overlap` 缺省为 0，旧输入保持原顺序；当前三维 inputs 显式设为 1。
  没有发送源或没有剩余块时沿用原顺序。二维输入缺省不启用，但显式开启已做数值回归。

三维 inputs 的 `performance.osi_detail_timing` 改为 0，以免嵌套计时同步破坏重叠。
开启细分计时仍可用于诊断，但不适合评价端到端收益；`osi_* = 0` 表示未测量。
重叠时 `collide` 包含发送源碰撞及剩余碰撞的提交时间，`comm` 区间包含剩余 GPU 碰撞执行；
这些时间不能解释成独立 GPU 内核成本或直接相加，性能采用 `total`。
下述控制与候选均关闭细分计时，收益不包含改变细分计时默认值的效果。

## 性能方法与结果

AMReX 26.06、FP64、D3Q27、CUDA 12.8、GCC 11.3、HMPI 1.2、单节点 A100-PCIE-40GB。
固定 Re3200、128³、BF16；真实 AMR 为 level 0/1/2、每32步 regrid、六面物理边界。
每场景从同一96步 checkpoint 推进到608，丢弃128步首窗口，统计192–576七个64步窗口，共448步。
同作业内固定节点和 GPU，三轮正/反/正配对；每指标逐窗口取较慢 rank 再累计，不相加 rank 时间。
逐窗口碰撞 calls/launch_cells、全部工作计数和实际 AMR 层级一致。

表中为配对总耗时下降百分比的中位数，负值表示变慢。

| 布局 | GPU 数 | 异步候选 total 下降 | 三组范围 |
|---|---:|---:|---:|
| AMR，max_grid_size=128（当前生产设置） | 2 | **5.518%** | 5.046%–5.576% |
| AMR，max_grid_size=128 | 4 | **4.184%** | 3.440%–4.237% |
| AMR，max_grid_size=64 | 2 | **4.906%** | 4.615%–5.174% |
| AMR，max_grid_size=64 | 4 | **4.948%** | 4.859%–5.320% |
| AMR，max_grid_size=32 | 2 | 0.101% | −0.267%–0.109% |
| 单层全周期，max_grid_size=32 | 2 | 0.059% | −0.305%–5.403% |

最后两项没有稳定收益。块分类日志中的 cells 是碰撞发射点计数，包含随后被 mask 跳过的点，
不解释成 active cell 比例或可隐藏时间比例。64 布局的初始 level 0 全部为发送源；
level 1/2 有剩余块，初始 level 2 的剩余发射点比例约80%。

先测的同步候选只在 MPI 发送启动后执行并等待剩余碰撞，未覆盖 D2H 准备；
64 布局的2/4 GPU total 中位数分别下降 −0.783%/1.197%，没有采用。
两个候选各自与同节点控制比较，不跨节点比较绝对时间或将差值归因于单个 API。

作业617192为同步候选，617193为异步候选，617233补充生产128布局。
整理为正式开关后，617234使用同一可执行文件、同一源码，仅输入开关0/1不同：
重做数值验证，64布局2/4 GPU各一组total下降5.083%/5.178%；此单组核对不并入三组统计。

## 数值、构建和收尾

- 两个原型及最终程序各16组64步阶段对照，另6组最终分支对照，覆盖2/4 ranks、周期/物理边界、
  同步/分块 staging、三种插值、实际重叠与无剩余块；补充单rank、异步整块 staging及细分计时开启。
  38,711条阶段误差记录 Linf=0，4,710条来源诊断与控制一致；阶段完整性检查通过。
  这些三维阶段对照实际涉及level 0/1；level 2逐值证据来自下面单独的checkpoint检查。
- 每个上述数值集合另做192步动态新建、重建、清层、再次建立，以及64步三层 fixture。
  CPU独立读取每组三层checkpoint的10,948,608个DDF分量：level 0/1/2的全valid有限值Linf=0，
  active/covered非有限计数和非有限模式差异均为0。192步终态实际为level 0/1，不冒充终态三层。
- 正式DIM2/3构建通过；复用维度回归并显式开启重叠，617235的54次运行、42项检查及12组控制通过。
  二维和三维均存在实际可重叠布局，覆盖mode0/1/2、1/2 ranks、周期/各物理面边界、OSI/A-B和动态生命周期。
- 既有物理ghost来源差异保持不变（3D最大0.01352278042，2D为0.02019539653）；非物理来源无mismatch。
  没有据此声称全部物理ghost等价，也不证明长期物理解精度、多节点、CUDA-aware MPI或二维性能。
- 共254次程序运行全部退出0；六个作业均SUCCEEDED，审计核对指纹、阶段完整性、逐层工作量和汇总。
  正式2D/3D程序与实际测试副本SHA256一致。保留全部源码、输入、程序、命令、日志和checkpoint。
- 并行出现的`AmrCoreLBM_amr.cpp`、`OsiIndex.H`注释/格式及`AmrCoreLBM_osi.cpp`格式修改均保留、不提交。
  最终控制和候选包含同一份这些修改，隔离开关差异；头文件沿用CRLF，使用
  `git -c core.whitespace=cr-at-eol diff --check`检查。

详见[冻结实验与复现入口](../../runs/osi_collision_overlap_20261022/README.md)、
[完整审计](../../runs/osi_collision_overlap_20261022/audit.json)、
[最终指纹](../../runs/osi_collision_overlap_20261022/final_artifact_check.json)、
[维度回归](../../runs/dimension_refactor_617235/acceptance.json)。
