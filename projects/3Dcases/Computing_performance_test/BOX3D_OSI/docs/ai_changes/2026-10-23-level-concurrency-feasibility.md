# 粗细层并发的性能空间评估

## 目的与范围

评估当前 BOX3D_OSI 在同一 GPU 上并发不同 AMR 层的潜在收益。
基线为 `add3c22` 加任务开始时已有的三份用户修改，完整源码和指纹已冻结。
只在独立实验副本增加分层计时、NVTX 和碰撞重放，不改变正式求解器的推进与通信顺序。

## 方法与结果

作业617831，单节点 whshare-agent-96、A100-PCIE-40GB、CUDA12.8/GCC11.3/HMPI1.2、
AMReX26.06、FP64/D3Q27。固定 Re3200、128³、BF16、max_grid_size128、level0/1/2，
32步regrid，启用已有同层碰撞/通信重叠，host staging、2MiB分块、OSI细分同步计时关闭。

早期流场固定step96 checkpoint，推进至320，分析192/256/320三个64步窗口；
1/2/4 ranks各三组交替base/diag对照。成熟流场固定step256000 checkpoint，4 ranks推进至256128，
丢弃首64步，分析最后64步；两组交替对照。逐窗口核对各rank工作计数和逐层calls/launch_cells。
每指标逐窗口取较慢rank再累计，不相加rank耗时。分层诊断没有新增阶段同步。

表中均为耗时下降百分比。推进上限假设各层所有推进调用完全重叠且无资源竞争，
插值、平均、regrid等其余成本不变；各层1:2:4子步数已计入。
它还忽略跨层依赖、MPI生命周期与调度成本，不是完整AMR算法改写的全局上限。

| 流场 | GPU/ranks | 碰撞重放实测下降 | 推进完全重叠的总耗时下降上限 |
|---|---:|---:|---:|
| 早期 | 1 | 2.33% | 10.52% |
| 早期 | 2 | 2.63% | 10.90% |
| 早期 | 4 | 2.11% | 18.99% |
| 成熟 | 4 | 未做重放，避免额外显存副本 | 10.50% |

重放复用生产碰撞函数、真实Fab/掩码/ghost范围、phase和松弛参数，在同一step320快照上，
对照串行与每层独立stream；每次8个粗步等价碰撞，丢弃两轮预热，六组交替配对，GPU事件测全部stream完成。
输入恢复和结果对照不计入计时。它不执行通信、迁移、边界或平均，也不等同完整求解器并发。
因此2.1%–2.6%仅是碰撞重放收益，不能当成整程序加速。

Nsight Systems各rank分析连续8个粗步的时间并集：早期单卡kernel busy为96.31%；
早期双卡24.87%–25.65%，含设备拷贝43.33%–45.24%；早期四卡16.00%–18.55%，
含拷贝30.99%–40.18%。成熟四卡kernel busy为23.78%–25.34%，含拷贝34.83%–41.96%。
这说明多卡有明显等待窗口，但不是SM算力、occupancy或带宽利用率。
早期双卡ghost填充区间约占total的40%，包含粗到细通信、暂存、缩放和插值。

结论：此次实际碰撞并发只有几个百分点收益；成熟四卡整层推进重叠的乐观上限约10.5%。
当前证据不支持为纯kernel并发全面改写AMR调度。下述空闲归因补充将成熟场的优化重点进一步
定位到level2同层通信；粗到细传输仍有空间，但不应只根据早期场阶段占比优先于同层通信。
若进一步拆分细层内部/边界，其潜力须另测，不受上述“其余成本不变”模型约束。

## GPU空闲归因补充

重新分析617831成熟四卡trace，以GPU上没有kernel、memcpy或memset执行的时间为完全空闲。
在step256065–256072的连续8个粗步中，以rank0为例，窗口2.17755秒，完全空闲1.34647秒：

| 空闲发生的阶段 | 空闲时间 | 占全部空闲 |
|---|---:|---:|
| level2同层通信 | 970.46 ms | 72.1% |
| level1同层通信 | 134.85 ms | 10.0% |
| level1到2的ghost填充 | 169.16 ms | 12.6% |
| level2到1的平均下传 | 60.39 ms | 4.5% |
| 其他 | 11.60 ms | 0.9% |

四个rank各自有66.3%–90.3%的完全空闲落在`FillBoundaryOsi`范围内；这些比率不相加rank时间。
rank0在8步内D2H共4.730GB、H2D共4.262GB，合计3584次CUDA拷贝；这是GPU与主机之间的流量，
不是网络字节数。2MiB分块策略用于同层FB，粗细层CPC仍走整块暂存/收发/等待路径。

为进一步区分CPU/MPI区间，作业617837使用同一冻结程序、输入和成熟checkpoint，增加MPI/OSRT跟踪，
推进256001–256073，只分析同样的256065–256072八步，四rank启动器退出0，rank0记录finalized，调度SUCCEEDED。
补采节点是whshare-agent-174，与原始whshare-agent-96不同，且跟踪项增加；不比较两次绝对耗时或归因性能差值。
补采中各rank有92.2%–97.1%的GPU完全空闲与CPU执行`MPI_Wait`重合。
按requestHandle与最近的Isend/Irecv关联，接收请求占Wait空闲的63.7%–91.5%，其余为发送请求。
示例rank0的空闲1.56234秒中，有1.45700秒位于`MPI_Wait`，其中接收0.92783秒、发送0.52918秒。

源码对应`FillBoundaryOsi`的分块接收/发送请求逐个`message.wait()`，以及插值/平均使用的
`OSI::parallel_copy_mpi`中的`wait_messages`；AMReX `Message::wait()`最终调用`MPI_Wait`。
host staging的链为GPU pack→D2H→MPI收发与完成等待→H2D→GPU unpack，后续推进消费接收结果。
GPU只在MPI等待期间仍有其他已排队任务时才能保持活跃，现有剩余Fab碰撞重叠未覆盖全部等待。

MPI_Wait内部包含协议推进、主机端传输和等待对端等可能成本，不能等同于网络延迟。
这批证据也未进一步拆分对端准备延后、MPI内部拷贝与具体传输协议的贡献。
统计的8步没有regrid，也关闭了PlotFile/checkpoint输出，因此不能将本批空闲归给它们。
分析脚本做GPU活动并集取补集，再与互斥外层NVTX阶段、CUDA及MPI API区间相交；
没有把整个通信区间都计作GPU空闲，也没有把CPU等待GPU拷贝的时间重复计为GPU空闲。

原样本与补采结果分别见
[原样本空闲拆分](../../runs/level_concurrency_20261023/job617831/mature_r4_trace/idle_breakdown.json)和
[MPI补采拆分](../../runs/level_concurrency_20261023/idle_mpi_job617837/idle_breakdown.json)。

## GPU直传与AMReX通信路径补充

2026-10-23在编译节点加载与生产相同的HMPI1.2模块后，只读查询`ompi_info`，
`mpi_built_with_cuda_support:value:false`、`opal_cuda_support:value:false`。
因此当前生产环境不能只将`lbm.osi_mpi_device_direct`改为1就视作CUDA-aware MPI已可用。
查询命令、实际ompi_info路径和结果见[能力记录](../../runs/level_concurrency_20261023/hmpi_cuda_capability_20261023.txt)。
这是MPI构建能力检查，不是另一次GPU传输性能测试。已有独立CUDA-aware实验入口仍未验收。

实际AMReX26.06源码：普通`FillBoundary`立即依次调用`FillBoundary_nowait`和`FillBoundary_finish`。
前者复用getFB计划，投递接收、GPU打包、发送，并处理本地复制；后者等待接收、GPU解包、等待发送并释放缓冲。
应用须在nowait和finish之间安排独立工作，才能额外隐藏应用计算；nowait自身也可能等待打包完成。
`ParallelDescriptor::Initialize`查询MPI支持并读取`amrex.use_gpu_aware_mpi`，
`The_Comms_Arena`据此选择device或pinned host缓冲。
`AMReX_FBI.H`的GPU pack/unpack直接访问该通信缓冲；非GPU-aware分支因此不等同于
本算例显式device聚合缓冲加D2H/H2D的路径。

OSI的`lbm.osi_mpi_device_direct`独立决定自定义MPI调用使用device还是host指针，
不由`amrex.use_gpu_aware_mpi`自动替代控制。自定义路径复用getFB几何计划，但pack/unpack必须处理OSI phase地址。
CUDA-aware接口支持设备指针不保证底层总是绕过主机暂存，具体IPC/P2P/RDMA路径仍取决于MPI、驱动和硬件。

现有碰撞重叠按整Fab分组：任何包含远端发送源的Fab先完整碰撞，随后pack，再异步提交剩余Fab碰撞，
与staging/MPI重叠。因而可隐藏计算只是剩余整Fab的碰撞，不是全部细层计算。
256064重网格后的level2布局确有剩余Fab（各rank18–38个），没有将整个样本误当作关闭重叠的路径；
发射点数量不等于实际活跃计算量或时间比例。
此前早期场同节点配对的128布局总耗时下降2/4 GPU为5.518%/4.184%，证明该机制已有收益；
不能外推为当前成熟场的同等收益或最佳可达重叠程度。当前证据支持“存在已验证的部分重叠，
通信完成链仍明显长于可隐藏计算”，不支持将全部MPI_Wait归为网络传输或直接当作可删除成本。

## 验证与证据边界

- 诊断副本全部九个算例翻译单元独立重编成功，仅复用未变AMReX对象；原始源码、输入、
  程序、构建命令/日志与SHA256保留。正式源码及用户已有修改未改动。
  最终审计后工作区另出现main.cpp缩进/空行调整，已核对为纯格式差异，保留且不并入本次提交；
  测量以冻结源码为准。
- 27次主测试应用运行全部退出0，617831调度状态SUCCEEDED；11份逐rank GPU时间线。
  base/diag总耗时差异的三组中位数分别为+0.023%、−0.052%、+0.621%，成熟两组为−0.673%；
  该差异只用于检查诊断扰动，不当成优化收益。
- 三种rank配置的step320 checkpoint共逐值检查每组1,133,678,592个DDF分量：active valid有限且差异0；
  covered有限值差异0，level1已有5,426,872个covered非有限分量的模式完全相同。
  重放21条rank/level误差记录Linf=0；这不证明长期物理解或完整并发算法正确性。
- 最初617829/617830因临时提交脚本执行权限而未运行应用，排除统计。初版checkpoint输出前缀
  已归档修正，所有本次step320文件移回实验目录；step96源checkpoint全文件SHA256未变。
  成熟源checkpoint同样留有全文件指纹。修正后的复现入口使用本地只读重启链接和本地输出前缀。
- 主测试内第一次Nsight Compute因名称过滤未采到kernel；补充617832正确匹配最大细层Fab后，
  被GPU硬件计数权限拒绝（ERR_NVGPUCTRPERM）。两次均不提供有效硬件计数，不能据此断言带宽或算力饱和。

完整方法、上限公式、原始数据、逐值检查和复现入口见
[冻结实验](../../runs/level_concurrency_20261023/README.md)及
[汇总](../../runs/level_concurrency_20261023/job617831/analysis.json)。
