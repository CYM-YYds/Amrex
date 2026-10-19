# job613495 Re=1000 三维中心线，256000 步

已通过用户本机 ParaView 的控制桥实际提取 `runs/20261015_171801_job613495/plt_256000`。
标准管线：AMReXBoxLibGridReader (Level=2) → ExtractBlock /Root → CellDataToPointData → PlotOverLine。
三维域 [0,1]³；采样线 (0.5,0.5,0) → (0.5,0.5,1)，1024 段、1025 点。

- [原始中心线 CSV](centerline.csv)：保留 ux、uy、uz、坐标、vtkValidPointMask 等字段。
- [对比图](comparison.png)：按 z/H、ux/U_lid 归一化，H=1、U_lid=0.05。
- [误差指标](reference_errors.csv)：在本地数字化 Fig.18(a) 参考点上对仿真线性插值，不外推。
- [提取元数据](paraview_export.json)：命令回传，包括 bounds、端点和采样数量。
- [完整报告](report.json)：数据源、哈希、归一化和指标。

1025 个采样点全部有效；三分量有限，坐标单调，底壁 ux≈0、顶壁 ux≈0.05。
相对本地 fig18a_velocity_profiles.csv：AMR 20 点 RMSE=0.0099868453；
Cortes & Miller 44 点 RMSE=0.0202637986，均以顶盖速度归一化。
未预先约定通过阈值，因此只报告差异，不判为严格数值验证通过或稳态收敛。
旧 160000 步 CSV 来源溯源不完整，仅叠加形状参考，不据其与新曲线接近判断时间收敛。
本次检查只覆盖该中心线，不等价于全三维场的有限性或逐值验证。

从仓库根复现（需要当前新版 ParaView 桥在线）：

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/data_post_processing/extract_run_centerline.py projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261015_171801_job613495
```

只使用已导出的数据重绘，在上述命令末尾加 `--reuse-export`，不要求 ParaView 在线。
