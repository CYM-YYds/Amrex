# AMR C 与 Cortes–Miller 中心线对比

输入为C job624717从静止推进至step128000、t1000的plt_128000，并非从B重启的C短测。Re1000，阈值0.25/0.5。由extract_run_centerline.py batch后端提取x=y=0.5、z=0..1，1024段、1025个有效采样点，0无效点，H1、U_lid0.05归一化。
仅保留归档参考CSV的Cortes_and_Miller_circle共44点，使用原始无量纲数据、不再缩放；完整来源和SHA256见reference_source.json。参考为现有Jaber Fig.18(a)数字化点，不是精确真解。

复用plot_centerline.py，6.4×5.8图幅、正方形绘图区，实线AMR C、空心圆Cortes & Miller，未修改绘图脚本。命令见plot_command.txt；数据、误差及输入指纹见同名CSV/JSON。44个参考点全部参与误差计算，RMSE0.0221623、最大绝对误差0.0856207（单位u/U_lid），不构成收敛证明。原图保留。

原始导出、采样检查和日志见profile/，重绘图位于此目录及images/job624717_C__centerline_t1000_with_Cortes.png。全部复现材料保留。验证：1025点掩码与有限值、参考点筛选计数、实际绘图及目视布局检查、git diff --check。
