# ParaView 控制桥新增三维中心线导出

- 目的：按用户要求，使用当前远端 ParaView 处理 AMReX 三维数据。
- 范围：控制桥客户端、投递器和说明；不修改连接脚本的现有文件模式变化。
- 实现：新增白名单 export_centerline，复用已有 AMReX 读取器及标准 CellDataToPointData/PlotOverLine 管线，远端 CSV 输出；恢复原活动源，失败清理此次管线。投递器支持输入/输出路径、层级、采样段数、等待时间。inspect 公布能力列表。
- 验证：仓库 .venv Python 语法及模拟 ParaView 下的中心线端点、参数限制、17 位精度、活动源恢复和失败清理通过；暂存 diff 检查通过。
- 边界：当前窗口旧桥需要加载新版后才支持该动作，真实 AMR 读取与 CSV 导出尚待联调。没有宣称真实数据已成功提取。
