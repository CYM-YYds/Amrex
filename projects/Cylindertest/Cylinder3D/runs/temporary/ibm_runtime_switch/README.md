# IBM 运行参数验证

临时实验：验证 IBM 开关、默认兼容、球体/圆柱施力和重启路径，不作为收敛或性能实验。
保留 inputs、命令、源码差异、可执行文件、构建日志、运行日志、检查报告与 checkpoint/PlotFile。
preexisting.ex 及 stale_case_objects 为现场旧产物备份，不能当作当前源码编译结果。

最终验收：626182（matrix）、626183（negative/legacy），对照 626178。
结果见 verification.json；最终可执行文件 SHA256 见 executable.sha256。
此前失败和中间轮次分别保留在 failed_coarse_index_wrap/、before_area_unit_fix/。
