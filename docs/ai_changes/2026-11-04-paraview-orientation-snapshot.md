# GUI 方向轴位置快照

目的：批处理复用用户拖动后的方向坐标指示，避免只加载PVSM后回到左下角默认位置。
范围：ParaView skill 的 snapshot_state 和生成客户端，不修改GUI布局或数据。
实现：复用快照回执，逐视图读取实际包含 vtkPVAxesActor 的 renderer，
另存归一化 viewport；不将主renderer中的 CenterAxes 或 GridAxes误判为方向轴。
批处理先禁用首次渲染自动复位相机，在初始化renderer后恢复方向轴viewport。
验证：实际GUI回传视图15809的viewport为
[0.13904034896401346,0,0.2839205232559527,0.2499999999999996]；
编译节点真实C数据单帧导出确认位置和相机同时保留。
Python语法、生成一致性检查和现有66项unittest完成（15项跳过）。
此为布局和导出验证，不作为流场数值验证。
