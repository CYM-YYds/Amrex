# ParaView 整体计算域框线

目的：在当前涡量视图中用单个立方体框替代 AMR 分块框线。

范围：ParaView skill 的协议、运行层、排版模块及生成客户端；不改求解器。

新增受限 domain_outline 操作，复用现有排版事务和原始读取器全域 bounds，
创建黑色 Wireframe Box 并隐藏目标视图内可见 Outline；保留相机、时间和数据管线。
明确指定 existing_source 时可复用已创建的对应 Box；固定域适用，不跟随移动域更新。

验证：打包一致性与 Python 语法检查；现有 66 项 unittest 完成（15 项跳过）。
当前 Windows GUI 热更新成功，在 C/job624717 的视图15809执行，返回 bounds=[0,1,0,1,0,1]、
6面 Box、Wireframe、时间25，两组原 Outline 显示已隐藏；再次显式复用成功。
首次颜色关联错误由事务回滚后修正。此为实际 GUI 操作回执验证，不是数值正确性验证。
截图未能在共享目录确认落盘，因此不把截图作为验证证据。
