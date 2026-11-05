# job624717 耗时占比

输入为 `runs/a5_C_t1000_gif/job624717/C/run.log`，覆盖 1–128000 步、物理时间 0–1000，共 40 个 3200 步窗口。输出 PNG、PDF、SVG、CSV 与含输入校验和的 JSON。

占比按插值、碰撞、迁移、平均下传、通信、边界、数组交换与 `regrid_time` 累计之和 3483.8674 秒归一化。数组交换为零，图中省略。日志 `perf(s).total` 累计 3483.0883 秒，两者相差约 0.022%，因此图表示分项计时构成，不是严格互斥的完整程序墙钟分解。网格重构含逐步检查与触发后的工作；无外部参考数据。

在仓库根运行：

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/plot_timing_pie.py --log projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/a5_C_t1000_gif/job624717/C/run.log --output projects/3Dcases/Computing_performance_test/BOX3D_OSI/data_post_processing/results/job624717_timing_pie/timing_pie.png
```

复用现有 `plot_run_performance.py` 的日志解析；新增临时绘图入口仅负责本次饼图，作为唯一生成脚本保留。已核验窗口连续性、步数、占比合计及图片渲染。
