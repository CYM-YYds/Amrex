# 固定单层的同步与碰撞mask独立测试

## 目的与范围

分别测试减少重复GPU同步和移除碰撞核mask判断的影响，不把两项叠加。
生产源码及生产程序不改写；候选只在冻结实验副本中构建，均显式拒绝 `max_level>0`。
本轮不包含二维、DOUBLE、AMR、粒子或其他碰撞公式的验收。

实验目录：[runs/temporary/uniform_kernels/round1](../../runs/temporary/uniform_kernels/round1)。
目录属于临时短测；保留源码、patch、输入、程序、哈希、构建日志、运行命令、原始日志、
checkpoint和报告。复制的 `build/tmp_build_dir` 为明确可删除的中间缓存，收尾清理。
基线来自当前工作树，含用户既有修改；不将结果描述为干净HEAD验收。

## 两个独立候选

- `sync`：关闭comm/stream/boundary/swap主阶段计时器；去掉Collide在MFIter析构后的
  额外同步，以及单rank本地复制后的同步；在每次 `AdvanceOneLayout` 末尾统一等待。
  保留MFIter默认的多stream前后等待、MPI内部等待、D2H/H2D跨stream依赖和缓冲区生命周期等待。
  已核对AMReX 26.06的 `MFIter::Finalize` 会等待已使用stream并恢复默认stream，
  本地复制、phase更新和薄面边界在默认stream有序执行。
- `nomask`：只删除Collide三个布局/公式分支中的 `has_fine_level && covered && !interface`
  设备判断。继续使用原来的碰撞公式、OSI地址映射、Fab范围、block大小和同步顺序。
  由于实验只允许固定单层，不会误计算AMR covered区域；本次不向生产代码增加平行实现。

同步候选中的comm/stream/boundary/swap秒数为未测量，不是零成本；端到端计时
包含每步末尾的GPU等待。MFIter调度和寄存器公式均不改变，因此不能据本轮排除其他同步
方案、批量碰撞、block大小或OSI地址优化的价值。

## 构建与复现

[prepare_uniform_kernels.py](../../scripts/temporary/prepare_uniform_kernels.py)
冻结baseline/sync/nomask三套源码并生成两个patch，复制一份独立编译缓存；
[build_uniform_kernels.sh](../../scripts/temporary/build_uniform_kernels.sh)
通过已有编译入口依次构建三维FLOAT CUDA程序，每轮强制重编两处实验源，避免候选混用。
三个程序分别冻结，不复用上轮单层调度实验的旧程序。

复用 [uniform_dispatch.sh](../../scripts/temporary/uniform_dispatch.sh) 的MPI、命令记录、
输入覆盖和退出保护，新增 `UNIFORM_KERNEL_EXPERIMENT=1` 实验模式；
[analyze_uniform_kernels.py](../../scripts/temporary/analyze_uniform_kernels.py)
复用上一轮的窗口解析及较慢rank聚合，另行检查本轮矩阵，不新增平行提交框架。

## 验证和性能口径

- 28组数值运行：64³、多Fab、1/2 ranks、A-B/OSI、非周期/全周期；
  另含两个16步双rank周期锁步检查和两个从baseline第32步checkpoint恢复的检查。
  18次checkpoint对照要求全部valid/active的L1/L2/Linf差异有限且为0，末态检查有限性。
  双rank小块布局覆盖发送源和剩余块并存的碰撞通信重叠路径。
- 数值检查通过后才进入性能。三程序独立运行，不执行组合候选；每种布局用六种排列顺序
  各一次，共享同块baseline做两项独立对照。每次1200步，排除首200步，统计201–1200步。
- 性能为三维FLOAT D3Q27、OSI、非周期、固定单层、regrid=0、嵌套计时关闭。
  覆盖128³单块、128³块32、256³块128及双rank128³块32，包含通信重叠布局。
- 采用每窗较慢rank的 `total` 再累加，检查每层碰撞调用数和发射单元数相同；
  不累加rank时间，不相加重叠的阶段耗时，不把初始化、I/O或收敛检查计入本指标。

三套三维FLOAT CUDA构建均成功。作业621544在whshare-agent-185的A100-PCIE-40GB上运行，
28组数值运行全部通过，18次checkpoint对照的全部valid/active差异范数均为0。
两个双rank锁步和两个restart检查通过；双rank64³/块16的每个rank有16个发送源Fab和
16个剩余Fab，确认覆盖通信重叠回调。

原始编译日志中OSI碰撞入口三者均使用168个寄存器/线程；nomask入口的cmem[0]报告
从672降至536 bytes。原单层核本就通过短路条件跳过mask数组读取，本候选减少的是
设备端条件及其捕获参数，不能将收益解释为每个单元少读两张mask或寄存器数下降。

## 性能结果与结论

72次性能运行全部正常退出，工作计数逐对一致。下表为六对端到端耗时变化中位数，
负数表示候选更快；完整范围及逐对秒数见
[report.md](../../runs/temporary/uniform_kernels/round1/job621544/report.md) 和
[summary.json](../../runs/temporary/uniform_kernels/round1/job621544/summary.json)。

| 网格 | 块大小 | ranks | sync变化 | nomask变化 |
|---|---:|---:|---:|---:|
| 128³ | 128 | 1 | -0.723%（跨零） | -1.814% |
| 128³ | 32 | 1 | -0.688% | -1.243% |
| 256³ | 128 | 1 | -0.160% | -1.594% |
| 128³ | 32 | 2 | -0.994% | -0.430%（跨零） |

- 单卡nomask在三种布局的六对范围全部为负：分别为[-2.131%,-1.483%]、
  [-1.294%,-0.853%]、[-1.609%,-1.500%]。256³每步节省中位数约0.108670 ms。
  双rank范围[-1.680%,+0.288%]，没有稳定收益证据。
- sync在128³块32、256³块128和双rank布局的六对全部为负，绝对节省中位数分别约
  0.011013、0.010947、0.016008 ms/步。128³单块范围[-1.756%,+0.645%]，不据此宣称稳定加速。
  同步节省接近固定开销，256³上的比例降至0.16%。
- 本轮支持优先考虑单卡固定单层的无mask碰撞入口；两项均为小幅优化，不能据此认定
  AMR条件判断是均匀网格推进的主要瓶颈，也不能把两项结果直接相加成组合收益。

本次任务只要求独立测试，两个候选保留在实验副本，未改生产数值路径或默认配置。
将来整合须保留AMR的covered/interface筛选，不能直接应用本次删除所有mask判断的补丁。
本轮短程数值与单节点计时不外推为长程物理精度或其他GPU上的收益。

三维FLOAT CUDA构建、Shell语法、Python AST、新增记录链接与diff空白检查通过。
生成源码、构建及分析入口是本实验的复现依赖，随结果保留；清理本次复制的编译缓存。
