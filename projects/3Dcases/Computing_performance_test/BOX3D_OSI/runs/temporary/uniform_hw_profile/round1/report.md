# 256^3 lev0 硬件剖析结果

## 基线

- 程序来自job621699冻结实验；SHA256=`2b381410ec930e61e1272d46cb60029b1f7b9a042bfaa546c13094f3fb3aa0b3`。输入与源码快照保存在job目录；step=256000 checkpoint只读复用历史`runs/20261030_102153_job621699/chk00256000`，未复制到本轮目录。
- job621705在`whshare-agent-92`、NVIDIA A100-PCIE-40GB、单rank/单GPU运行三次，每次从同一checkpoint推进400步。关闭PlotFile、checkpoint和收敛检查，report_int=100。
- 各轮step 256100首窗口受restart/初始化影响，排除后汇总step 256200/300/400共9个窗口：平均`perf(s).total=0.605482 s/100 steps`（6.055 ms/step），累计更新量除以累计total得到`MLUPS_total=2770.884`，`collide=0.509656 s/100 steps`。逐步GPU事件均值为碰撞5.085 ms/step、边界0.939 ms/step。
- 当前程序会对eligible均匀单层推进自动逐步记录CUDA事件，不能通过inputs关闭；因此这组基线包含该计时开销。此前同节点对照显示约0.2%，本轮数字不标为完全未采样吞吐。
- GPU和节点信息见`job621705/gpu.txt`、`node.txt`；三个run.log及退出码在各自baseline目录，作业621705终态SUCCEEDED。

## Nsight Compute

作业621705计划跳过16次匹配发射后采集一个碰撞kernel；应用启动后驱动即返回`ERR_NVGPUCTRPERM`。未能验证kernel匹配或稳态采集是否生效。Nsight退出码为1，没有`.ncu-rep`和有效硬件计数器。因此本轮不能给出DRAM带宽、sector效率、achieved occupancy或warp stall结论；需要管理员开放性能计数器权限后重试。

调度器SUCCEEDED仅表示批处理脚本完成；`status.tsv`中的`ncu_collision=1`才是采集失败的直接凭证。

## Nsight Systems

作业621706在`whshare-agent-67`、A100-PCIE-40GB采集30步，终态SUCCEEDED。原始`trace.nsys-rep`、SQLite数据库和统计输出保存在`job621706/nsys_uniform/`。

- GPU kernel汇总：30次碰撞kernel累计187.874 ms，均值6.262 ms；30次物理边界kernel累计30.175 ms，均值1.006 ms。碰撞占采样GPU kernel总时长80.2%，边界占12.9%；分母包含初始化kernel，且30步包含预热，不能作为稳态推进的严格占比。Nsight插桩会影响绝对kernel时长，不与未剖析基线直接比较。
- SQLite kernel元数据显示碰撞为256 threads/block、168 registers/thread、`localMemoryPerThread=0`；边界为256 threads/block、86 registers/thread。register数是launch元数据，不代表实际occupancy。
- CUDA API统计含93次`cudaStreamSynchronize`，累计321.2 ms；API等待与kernel执行重叠，不能相加作为每步成本。Nsight Systems不提供本次所需的DRAM读写字节或硬件occupancy计数器。

## 结论

确认主耗时仍为碰撞kernel，launch元数据与ptxas的168寄存器结果一致；边界kernel约占GPU kernel时间13%。本轮未取得真实内存事务、DRAM带宽、occupancy或stall指标，无法判定差距主要来自OSI事务效率、寄存器资源限制还是指令吞吐。下一步需先获得Nsight Compute计数器权限，再基于计数器选择单因素A/B。

## 失败尝试与保留边界

job621704三轮基线正常完成，但首次Nsight启动因子目录缺少inputs退出6；修正链接后以job621705重试。两节点基线不混合统计或作代码加速对照。原始失败日志和输入均保留。job621706是另一节点上的时间线补充，不与621705直接计算加速比。

本轮没有数值路径改动，也未新增逐值回归或物理精度验收。生成的`summary.json`按累计更新量/累计total计算，排除各轮首100步。重复的三个application.ex每个123361416字节，与正式冻结程序同哈希；已在用户确认后换成指向正式冻结程序的相对软链；原路径可用，哈希未变，共释放370084248字节。
