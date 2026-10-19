# 独立 pvbatch 提取 job613495 的 256000 步三维中心线

本次未使用本机 ParaView 或控制桥。运行元数据确认执行节点为 **whshare-agent-1**，
独立环境 ParaView 6.2.0，管线构建与导出耗时约 14.78 秒。

- 输入：`runs/20261015_171801_job613495/plt_256000`，Re=1000，AMR level 0–2。
- 采样：(0.5,0.5,0) → (0.5,0.5,1)，1024 段、1025 点，全部有效。
- 归一化：H=1、U_lid=0.05；参考数据为本地数字化的 `fig18a_velocity_profiles.csv`。
- [原始 CSV](centerline.csv)、[对比图](comparison.png)、[参考误差](reference_errors.csv)。
- [执行元数据](paraview_export.json)、[分析报告](report.json)、[程序日志](export.log)。
- [与桥接结果逐值比较](batch_vs_bridge.json)：1025 个位置、全部 9 个数值列按列名对应，
  最大绝对差为 **0**；检查容差 atol=1e-12、rtol=0。两个 CSV 的列顺序不同，因此字节哈希不同。

参考比较指标与桥接版相同：AMR 20 点 RMSE=0.0099868453，Cortes & Miller 44 点
RMSE=0.0202637986，单位均为 ux/U_lid。这证明本数据的中心线批处理可独立完成，
不证明全三维场逐值正确或已达到稳态收敛。

从仓库根复现（运行入口自动跳转编译节点）：

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/data_post_processing/extract_run_centerline.py projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261015_171801_job613495 --backend batch
```

只重绘可再加 `--reuse-export`，不会启动 ParaView。
