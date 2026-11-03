# AMR B/C、均匀512³与Cortes–Miller

在C_uniform512_Cortes图基础上另存更新版，原图保留。增加原B job621436在step128000/t1000的既有1025点中心线，红色实线；不使用B续跑t2000数据。C job624717为黑实线，uniform512 job623012为黑虚线，参考44点为空心圆。全图Re1000/t1000，H1、U_lid0.05，6.4×5.8图幅与正方形绘图区。

复用plot_centerline.py已有color/linestyle机制，仅增加可选--profile-style LABEL COLOR LINESTYLE命令行参数，将指定样式写入JSON；未指定时保留原样式。未新增提取或绘图入口。数据、SHA256、归一化和误差见同名JSON/CSV，命令plot_command.txt。
验证：Python语法解析、实际重绘、B有效掩码及有限值校验、目视红色曲线和图例，git diff --check。参考吻合不代表时间收敛。
