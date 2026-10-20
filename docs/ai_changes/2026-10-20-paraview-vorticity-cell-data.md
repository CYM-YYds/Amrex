# ParaView 涡量梯度使用原生单元数据

- 目的：避免 AMR composite 数据经过单元到点重建后，Gradient 在部分数据块找不到输入数组。
- 影响范围：仓库 ParaView 桥接涡量视图及其参数说明；不修改求解器或 PlotFile。
- 改动：有速度单元数组时在单元关联上计算梯度和显示涡量；仅点数组输入时保留点关联路径。补充涡量色标和切片相机设置。
- 验证：ParaView 6.2.0 pvpython 解析旋转场与真实 PlotFile 回归通过；当前 Windows GUI 热更新后创建视图成功，报告 CELLS 关联及涡量范围 `[0, 25.6]`。
