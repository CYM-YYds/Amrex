# 均匀网格异步阶段采样

## 目的与范围

将单GPU固定lev0 OSI推进中原先混合的边界、相位、CPU提交和同步等待分开记录，不恢复逐阶段同步。只修改BOX3D_OSI计时与对应实验入口，不改变碰撞公式、边界公式、推进顺序或原有依赖。

现有ScopedPerfTimer退出时同步GPU，不能语义兼容地用于本次异步计时，因此新增事件池；沿用PerfStats的窗口生命周期和现有推进/报告入口，不新增替代推进路径。实验复用uniform_dispatch.sh及其分析器，不复制新提交入口。用户已有inputs、AMR判据、MLUPS文档和后处理修改均未改动或纳入提交。

## 主要改动

- 新参数performance.uniform_sample_int，默认0关闭；N>0按本次启动后的level-0推进次数每N次采样，与窗口清零无关。当前仅CUDA、单rank、max_level=0、OSI且uniform_reduce_sync实际生效时启用；其他模式明确输出eligible=0、samples=0。
- GPU事件按Fab实际stream包围碰撞发射；默认stream分别包围相位更新和物理边界。仅在已有MFIter多stream等待和已有步末同步后查询事件，无新增cudaEventSynchronize/streamSynchronize；未完成事件立即报错，禁止隐藏补同步。
- 事件池由AmrCoreLBM唯一拥有，首次需要时创建，完成后跨步复用，按采样步重置已用事件数；事件不持有场数据指针，无网格地址缓存。窗口ResetPerfStats只清统计，析构释放池。
- CPU分别记录碰撞准备/提交、MFIter析构（含既有同步和收尾）、通信入口、相位准备/提交、边界准备/提交、原有步末等待和函数内其余时间。step是AdvanceOneLayout样本墙钟，等于上述CPU分账之和；collect为等待后事件读取/统计成本，另列且已计入外层solv/total。外层AdvanceLevel/main的调度不在sample.step中，仍由solv/total覆盖。
- 每窗口输出uniform_sample_gpu(s)、uniform_sample_cpu(s)，所有值都是实际样本累计秒数，不能当成全窗口总量；除以samples得到样本步平均，无样本时不做除法。
- GPU事件区间可能包含stream空隙/host发射延迟，不是Nsight纯kernel活跃时间；collide_sum为跨stream区间和，不等于关键路径墙钟。CPU等待与GPU执行重叠，禁止跨两行相加；MFIter析构也不全部是纯等待。全周期边界可能只有空事件区间，不表示执行物理边界kernel。

## 验证

实验：[runs/temporary/uniform_sampling/round1](../../runs/temporary/uniform_sampling/round1/README.md)，临时短测，保留源码/输入/程序/命令/编译日志/原始输出/checkpoint/分析。节点whshare-agent-82，A100-PCIE-40GB，3D FLOAT，程序与输入冻结。

- 标准compile.sh两次因编译节点SSH被远端关闭而未构建；调度作业621691在已分配节点以同一算例GNUmakefile执行make -j8 DIM=3 PRECISION=FLOAT，CUDA/MPI编译链接通过。
- 25个求解子运行全部exit_code=0并正常finalize。批量作业最终状态FAILED，原因仅为所有运行结束后的分析器错误地解包已有timing()返回字典；修正后对保留数据离线分析通过，原脚本与失败日志保留，不将批量作业误标SUCCEEDED。
- 5组checkpoint对照：单Fab、多Fab、全周期、两rank及动态AMR，所有层valid/active的Linf/L1/L2均为0；AMR第32步实际创建level1。
- interval=7在连续两32步窗口、以及从64步restart后的两窗口，样本数均为4、5；每步采样的单rank均匀网格每32步窗口为32样本。所有CPU分账与sample.step一致，事件查询均完成且时间有限、非负。
- AMR、多rank和OSI/A-B锁步模式实际输出eligible=0 samples=0；锁步逐阶段检查无mismatch。该验证不等于其他模式的GPU采样实现或长程物理精度验证。未新增2D/DOUBLE/CPU构建验证。
- Shell语法、Python语法解析、README/记录路径和git diff空白检查通过；CRLF源文件保持原换行，以core.whitespace=cr-at-eol检查。

## 同节点交替开销与采样结果

OSI非周期单Fab，采样关闭与interval=100使用同一程序、同一节点，3组交替顺序；各跑1200步，排除首200步，只比较201–1200步的完整total；每个采样运行纳入10个实际样本。GPU/CPU时间不参与端到端开销的加减推算。

| 网格 | 开启采样的耗时变化中位数 | 三组范围 |
|---|---:|---:|
| 128³ | -2.010% | -3.438% 至 +1.067% |
| 256³ | +0.088% | -0.095% 至 +0.182% |

128³有明显运行波动，不能把负变化认定为采样加速，也无法给出可靠的正开销估计；256³本轮差异很小，仅3组短测，不作为任意环境的开销上限。

本轮256³三组共30个样本平均：GPU碰撞区间和5.042620ms、物理边界0.934298ms、相位更新0.006383ms；CPU碰撞提交0.013656ms、MFIter析构5.042369ms、相位提交0.005950ms、边界提交0.004429ms、步末等待0.939764ms、采样step=6.006369ms、collect=0.009153ms。这些量的不同时间视角不能相加；事件读取成本仅发生在采样步。该结果是新短测的实际拆分，不能精确回填旧节点128000步日志中未测量的阶段。

使用方式与字段定义同步至算例README；完整逐组数据位于job621691/sampling_summary.json。
