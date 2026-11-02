# 现有 GUI 读取器原位扩展时间序列

目的：修复向用户提供的代码依赖固定注册名 `plt_128000` 的问题。当前窗口实际 AMReX 读取器名为 `plt_..`、ID=13464，原输入只有 plt_128000、TimestepValues=[1000]。原代码 FindSource 名称不匹配，无法取得该读取器；未取得用户 Python Shell 错误文本，不排除同时存在其他粘贴或执行问题。

范围：ParaView 桥协议、CLI、运行层、数据操作模块、生成客户端及 GUI 用法说明。新增限定动作 `set_plotfile_series --source NAME --files ...`，精确匹配唯一 AMReX 读取器，要求同目录且步号严格递增；不接受任意 Python。原位更新 FileNames，检查逐文件物理时间，更新动画、TimeKeeper 与视图到末帧，仅渲染活动视图。异常时尝试恢复文件与时间属性；服务断线不保证回滚。复用现有桥接与打包机制，不重建排版或下游算法。

输入检查：B 目录 40 帧，step=3200..128000、间隔3200；Header 物理时间25..1000、间隔25；均为三维 ux/uy/uz。逐层 Cell_H 中共4008处 FabOnDisk 引用，文件存在且起始偏移小于文件长度。此项不是逐值数值验证。

验证：打包检查及 Python 测试通过，56项测试、12项条件跳过。GUI客户端从23热更新为24，单进程服务。动作回执 id=555b7db637d04178b5459ee011d30733 成功，40个文件与40个递增物理时间，TimeKeeper和两个视图均为1000，末帧渲染成功。保留当前管线及用户排版；未逐帧渲染全部40帧，不宣称全时序数值正确或收敛。

证据保留：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/a5_B_three_plot3200/job622356/B/figures/` 下 time_series_inputs.json、time_series_gui_result.json、time_series_gui_inspect.json。当前服务日志在 `.tools/paraview-mpi/logs/launch-db99785c50004579867fd5798d17351d/pvserver_20261102_162744_dBQRzc.log`，本次未见通信标签错误。
