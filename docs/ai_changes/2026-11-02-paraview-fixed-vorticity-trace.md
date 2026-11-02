# 固定 2DVorticity 脚本的 GUI 应用入口

目的：将用户已有 `后处理脚本/2DVorticity.py` 的排版直接用于指定 PlotFile，避免用其他排版工具重建近似效果。用户明确允许扩展桥接工具。

范围：ParaView 桥接协议、运行层分发、排版操作模块、单文件打包器与 GUI 使用说明。不修改求解器或原始排版脚本。

实现：增加 `apply_2d_vorticity_trace --input /absolute/plt_STEP`；打包器从原始脚本嵌入唯一源码，命令不能指定 Python 或脚本文件。要求空数据管线；AST 只替换读取器输入及注册名，随后按实际 TimestepValues 同步时间，激活原脚本的第二个出图视图。仅支持单帧。脚本异常可能保留部分新对象；不保证断线时事务恢复。

验证：客户端重新打包及 --check 通过；49 项桥接测试通过（10 项因当前环境无 ParaView/PythonQt 等条件跳过），新增 CLI 输入字段与原始脚本嵌入一致性检查。当前 Windows GUI 热更新成功，版本 22，cs://localhost:11115。

现场边界：对 job622356/B/plt_128000 的命令于 2026-11-02 14:32:49 发出；Header 验证 ux/uy/uz、三维、AMR 0–2 层、step=128000、time=1000。14:33 服务端出现 Tag mismatch（got 1, expecting 188968；随后 got 55630, expecting 1）并退出，未收到该操作成功回执，不能宣称 GUI 加载成功或数值验证通过。需重新建立 GUI 连接再现场验收。

证据：`.tools/paraview-mpi/logs/pvserver_20261102_105259_zrob98.log`；旧会话 `session-f44edd611f8441fdac27bec8ec776dad` 的 command.json/result.json（结果仍为前一查询，不视为本次结果）。保留运行数据与日志。
