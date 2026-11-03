# AMR C、均匀512³与 Cortes–Miller 中心线

在原AMR C/Cortes图基础上另存，原图保留。复用plot_centerline.py，6.4×5.8、正方形绘图区，黑实线AMR C、黑虚线Uniform512³、空心圆Cortes & Miller；无绘图脚本修改。

C来自job624717，step128000/t1000，沿用results/C_t1000_Cortes/profile/centerline.csv。
均匀512³来自job623012/step00512000/plt_512000，step512000/t1000、最高level0；completed标记、40段状态、末段exit0和Header已确认。全部3个速度输出文件均与transfer_manifest.json核对大小和SHA256通过，证据uniform512_source_check.json。与先前失败的job622116不混用。

使用仓库ParaView批处理在whshare-agent-1单进程提取，max-level0、沿x=y=0.5、z0..1，1024段/1025点，全部有效且有限。归一化H1、U_lid0.05，两组Re1000、同物理时间。导出记录在profile512/，提取与绘图命令分别extract_command.txt/plot_command.txt。

参考仍只使用44个Cortes_and_Miller_circle既有数字化点（Jaber Fig.18(a)），沿用原图筛选CSV，不把数字化点当作精确真解。误差表均为u/U_lid量纲，全部44点参与：C RMSE0.0221623，512³ RMSE0.0200680；这些局部参考吻合指标不证明时间收敛或全场网格无关性。

已核对有效掩码、坐标端点、有限值、参考点数并目视图例及比例；数据、元信息、命令与图长期保留。
