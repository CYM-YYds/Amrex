# 512³均匀Q27到物理时间1000

正式计算，512000步，单GPU、FP32、Re1000、lev0、单Fab、128线程与坐标复用。末态输出速度和涡量/Q PlotFile，关闭checkpoint及收敛提前退出。沿用已验证的amrex.the_arena_init_size=0，最多8小时guard。源码、配置、程序与命令独立冻结；保留日志、显存采样、场输出及报告。

submit.sh为既有512³提交命令的参数化快照：UNIFORM_END_STEP和UNIFORM_REPORT_INT仅供同尺寸短程输出预检；默认完整512000步/report3200。预检输出与正式目录按job分开保留，不用短测替代长期结果。正式物理终止条件取步数和stop_time任一先达到；最终需核对PlotFile时间确为1000。
