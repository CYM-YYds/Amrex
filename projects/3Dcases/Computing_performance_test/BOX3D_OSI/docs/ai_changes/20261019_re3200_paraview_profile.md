# Re=3200 最新中心线与 Fig.18(b) 叠加

- 目的：为 job615364 最新 256000 步生成原 288000 步对照图的新版，保留历史图片。
- 改动：扩展现有 `extract_run_centerline.py`，根据实际输入及覆盖参数选择 Re=1000/Fig.18(a) 或 Re=3200/Fig.18(b)，拒绝其他 Re；复用现有 ParaView 导出、有效性检查、归一化和误差计算。Re=3200 不加载旧 Re=1000 曲线，保留三组参考数据、大号标记和图外图例。
- 提取：按 paraview skill 使用独立批处理，执行主机 `whshare-agent-1`，ParaView 6.2，AMR max-level=2，中心线 (0.5,0.5,0) 到 (0.5,0.5,1)，1024 段。H=1，U_lid=0.05。
- 验证：真实 PlotFile 导出成功；1025 个点全部有效，三分量速度及坐标有限，x=y=0.5，z 均匀覆盖 [0,1]；复用导出重绘成功，检查 PNG 和 `git diff --check`。所有参考点均在范围内；AMR/Cortes & Miller/Experiment 的 MAE 分别为 0.01371034/0.02703239/0.02910050。
- 输出：`data_post_processing/20261019_124153_job615364_step256000_paraview_batch/`，含实际步数命名 PNG、导出元数据、误差和来源哈希报告；原始 CSV 按现有规则留在本地。图像接近参考数据不构成稳态收敛证明。
