# C阈值 A-B/OSI 耗时柱状图

输入为最新job626524的A-B与OSI完整日志，同一冻结程序、同节点单GPU A100、Re1000、FLOAT D3Q27、C阈值、128³基础网格、max_level2。两组从静止到128000步/t1000，均分4个32000步区段，在32000/64000/96000步恢复。使用128线程碰撞，OSI包含坐标复用；无I/O系统调用跟踪。固定OSI→A-B各执行一次，不作为重复统计的稳定收益证据。

图为40个3200步窗口的累计秒数，不做cell归一化，不用末窗MLUPS反推耗时。沿用中文标题、图例、蓝橙分组、单坐标轴及分项＋总耗时；不展示数组交换与solv，不增加图内备注。regrid_time由ms转s，分项不构成total的严格可加分解。total包含重网格，排除初始化、恢复、流场/checkpoint输出及回传；无外部参考数据。

A-B累计total=3954.54705359秒，OSI=2753.80301297秒，本次OSI耗时减少30.36%。重构分别118.545301127/92.548844614秒。原始输入SHA256、统计范围及秒数见图片同名JSON/CSV，完整运行结果见[报告](../../../runs/C_optimized_t1000_20261105/job626524/report.md)。两组完成目标时间，末态均converged=0。

生成器复用plot_run_performance.py的窗口解析。产物：[PNG](../../images/C_AB_OSI_t1000_time_comparison.png)、[SVG](../../images/C_AB_OSI_t1000_time_comparison.svg)、[CSV](../../images/C_AB_OSI_t1000_time_comparison.csv)、[JSON](../../images/C_AB_OSI_t1000_time_comparison.json)。仓库根使用[command.txt](command.txt)完整命令复现。Python语法、40窗口覆盖和全部分项/total与626524汇总交叉核对；重构直接核对日志40个regrid_time窗口。
