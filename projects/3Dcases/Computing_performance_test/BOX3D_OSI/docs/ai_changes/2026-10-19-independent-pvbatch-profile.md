# Re=1000 中心线独立 pvbatch 提取与桥接结果对照

- 目的：验证无需本机 ParaView 或控制桥，也能自动启动远端 ParaView 后处理。
- 范围：仅为现有 extract_run_centerline.py 增加可选 --backend batch，默认 bridge 不变；新增独立结果目录，不覆盖旧桥接产物，也不修改计算输入或求解器。
- 路径：仓库 .venv 调用批处理入口，入口先 SSH 跳转 whshare-agent-1，再以 ParaView 应用自带 pvbatch 执行；完成后由 .venv 读取 CSV、计算指标并绘图。CSV 导出复用共享的 export_centerline 函数。导出失败日志保存到 export.log。
- 现场验证：job613495/plt_256000、Re=1000、level 0–2，独立 ParaView 6.2.0 在 whshare-agent-1 执行，管线与导出 14.7804 秒；获得 1025 个有效点。
- 一致性：与此前 5.11 系列桥接版的同一 PlotFile 中心线比较，1025 个位置、全部 9 个数值列最大绝对差均为 0（atol=1e-12、rtol=0）。CSV 哈希不同来自列顺序不同。参考 AMR 的 RMSE=0.0099868453、Cortes & Miller 的 RMSE=0.0202637986，均以 U_lid 归一化。
- 产物：data_post_processing/20261015_171801_job613495_step256000_paraview_batch/ 下保存 CSV、PNG、元数据、报告、比较报告及日志；元数据明确执行主机和独立可执行文件路径。
- 边界：已验证独立数据提取与后续对比绘图，不替代全场数值验证或收敛证明；ParaView 自身截图/动画渲染未测试。长时间 ParaView 不在登录节点执行。
