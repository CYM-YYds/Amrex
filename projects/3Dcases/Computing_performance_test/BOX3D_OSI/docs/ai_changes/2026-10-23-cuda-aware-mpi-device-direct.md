# CUDA-aware MPI 设备指针路径实测

## 目的与范围

验证 BOX3D_OSI 当前设备指针 MPI 路径，并在同一 CUDA-aware MPI 环境中测量其对成熟三层 AMR 流场的整步收益。
没有修改求解器源码、正式 inputs、生产构建/提交入口。冻结源码包含用户任务开始时的四个未提交文件，未将其并入提交。

生产 HMPI 1.2 构建不支持 CUDA；本实验独立使用 OpenMPI4.1.5 CUDA-aware、CUDA12.8、GCC11.3、sm80。
算例和 AMReX26.06 全部对象独立重编，避免混用 HMPI 编译对象。OpenMPI 的 CUDA 构建/运行能力查询均为 true。
主对照显式选择`--mca pml ucx`；UCX1.12.1 通过运行库路径加载，避免其模块切换 CUDA 版本；运行节点可用 cuda_copy/cuda_ipc transport。
这不单独证明每条消息实际使用 CUDA IPC，更不证明跨节点 GPUDirect RDMA。

## 验证

- 作业617843，单节点四卡，设备缓冲环形 MPI_Sendrecv：4B、4KiB、4MiB，各5次，全部rank逐值零差异，调度SUCCEEDED。
- 作业617844，当前三层 AMR，2rank，从同一step96 checkpoint推进至128，含实际重网格，分别测试2MiB分块host staging、不分块host staging和device-direct。
- canonical checkpoint逐值比较每组805,773,312个DDF分量：各层有限值Linf=0，active区域无非有限值。
  covered区域原有非有限分量level0为5,608、level1为3,176,116，模式完全一致；不能据此称全部存储值有限。
- 正确性样本验证三条传输路径之间的短程等价，不是OSI/A-B算法或长期物理解的完整验收。

## 硬件路径限制与补测

主测试节点190以及补测节点96的`nvidia-smi topo -p2p r/w`均显示所有跨GPU组合为CNS（Chipset not supported），没有可用的直接peer read/write能力。
节点190的查询在第二轮之后的rank0包装器中、exec应用及初始化之前一次性执行，不进入perf窗口；保留原包装器和采集说明。

独立补测617845选择同一OpenMPI提供的`--mca pml ob1 --mca btl self,smcuda,tcp`。
设备Sendrecv探针虽输出零数值差异并退出0，却反复报`cuIpcOpenMemHandle failed`、返回217和remote memory注册失败。
CUDA12.8本地`cuda.h:2626`定义217为`CUDA_ERROR_PEER_ACCESS_UNSUPPORTED`。
因此没有验收smcuda IPC路径；人工终止后续完整求解器运行，调度最终FAILED，全部排除性能统计，仅保留探针/拓扑证据。
冻结复现入口补充了IPC错误拒收门槛，避免把数值正确但有IPC错误的路径误当成CUDA IPC成功。

这些结果区分了CUDA-aware设备指针接口与硬件P2P能力。尚未进一步采集主UCX运行实际选择的每条消息协议，
不能把后续计时差额精确归因为某一份host拷贝、注册开销或网络延迟，也不外推到其他节点/更新通信栈或跨节点RDMA。

## 性能方法

同一作业、节点whshare-agent-190，4rank/4张A100-PCIE-40GB，驱动535.104.12（nvidia-smi显示CUDA支持12.2），编译/运行模块CUDA12.8，固定当前Re3200、128³基网格、level0/1/2、BF16、max_grid_size128、regrid32。
同一个step256000成熟checkpoint，每次推进192步；丢弃首64步，汇总step256128/256192两个64步窗口。
`perf(s).total`每窗口取最慢rank后求和，包含重网格、不含初始化/重启。阶段指标也分别取最慢rank，不将各阶段之和当作总时间。
三轮顺序为pipeline-host-device、device-host-pipeline、host-pipeline-device。

三种模式均显式设置`amrex.use_gpu_aware_mpi=1`、碰撞通信重叠开启、async_staging关闭：

| 模式 | osi_mpi_device_direct | osi_mpi_pipeline_chunk_bytes |
|---|---:|---:|
| pipeline，当前分块策略 | 0 | 2097152 |
| host，不分块 | 0 | 0 |
| device，设备指针 | 1 | 0 |

因此pipeline→device是当前分块策略与设备路径的整体比较；host→device保持分块均关闭，用于隔离设备指针路径影响。
三者的AMReX原生通信均使用同样的GPU-aware配置。本实验不以旧HMPI历史作业作为直传加速基线。

## 结果

主作业617844调度SUCCEEDED，3次正确性运行与9次性能运行全部退出0。
每层碰撞调用次数/launch_cells及perf_count逐rank、逐窗口一致；两个历史源checkpoint全文件SHA256均未改变。

以下为128个有效粗步的总耗时（秒），每个64步窗口先取最慢rank后求和：

| 模式 | 第1轮 | 第2轮 | 第3轮 | 中位数 |
|---|---:|---:|---:|---:|
| 2MiB分块host staging | 47.899 | 47.614 | 47.685 | 47.685 |
| 不分块host staging | 49.604 | 49.570 | 49.658 | 49.604 |
| device-direct（设备指针MPI） | 363.606 | 361.721 | 362.209 | 362.209 |

相对当前2MiB分块策略，设备指针路径三轮耗时为7.591–7.597倍，配对中位数7.596倍（耗时增加659.59%）。
相对同样关闭分块的host路径，配对中位数为7.297倍，说明差距不能仅由移除2MiB分块解释。

同层comm计时的中位数由22.103秒变为254.734秒；插值/平均的壁钟计时也增加。
这些计时包含依赖和同步效应，不是纯网络时间或GPU kernel时间，不能用来反推SM占用率。

本节点/通信栈的设备指针接口通过短程数据验证，但没有性能收益，不据此切换生产配置。
可见的等待窗口不能直接视作启用CUDA-aware接口后可删除的时间；应先确认平台支持的P2P/实际传输路径，再评估直传。
这里没有证明所有CUDA-aware MPI或具备P2P/NVLink的节点都会更慢。

完整结果见[分析JSON](../../runs/device_direct_20261023/job617844/analysis.json)，
[主节点P2P读能力](../../runs/device_direct_20261023/job617844/p2p_read.txt)、
[写能力](../../runs/device_direct_20261023/job617844/p2p_write.txt)，
[IPC补测日志](../../runs/device_direct_20261023/job617845/probe.log)。


## 复现证据

[冻结实验目录](../../runs/device_direct_20261023/README.md)保存源码、inputs、工作树diff、全新可执行文件及SHA256、构建日志、MPI能力查询、GPU/节点信息、全部命令和逐rank日志。
`run.py`先执行正确性门槛，再运行性能矩阵；`analyze.py`检查各层调用/发射工作量、汇总最慢rank，并复核历史源checkpoint全文件SHA256。
重启源通过本地只读符号链接使用，checkpoint输出前缀保持本次运行目录。
