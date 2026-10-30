# 256^3 lev0 硬件剖析（临时实验）

使用job621699冻结的程序、inputs、源码快照和最终checkpoint，先在同一GPU分配内执行三轮未启用Nsight的固定网格稳态基线，再分别运行Nsight Compute和Nsight Systems。求解器源码与正式配置不修改。

运行关闭PlotFile、checkpoint和收敛检查。当前源码对符合条件的单层OSI运行自动逐步记录CUDA事件，无法通过inputs关闭；因此基线包含该采样开销（此前同节点交替测得约0.2%），不能称为完全无采样基线。基线以`perf(s).total`为端到端指标；Nsight Systems时间仅作kernel/同步时间线诊断，不将剖析运行时间作为性能结果。Nsight Compute遇到计数器权限错误时保留错误证据，不尝试提权或更改节点权限。程序哈希、源码/输入快照、命令、节点/GPU信息、原始日志和报告均保留，结论见[本轮结果](report.md)。
