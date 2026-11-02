# ParaView 连接默认值与启动证据留存

目的：针对现场 8 rank GUI Tag mismatch、单 rank GUI 成功的结果，默认使用已通过的单进程服务，并让每次连接的启动命令、参数及日志可对应。此修改不宣称解决多进程 GUI 协议错误。

范围：ParaView skill 的 connect_paraview.ps1 及 bridge.md；后处理脚本/connect_paraview.ps1 兼容软链保持不变。未改求解器、管线或当前 GUI 会话。

改动：
- ServerProcesses 默认值由 4 改为 1，仍允许显式指定 1–256。
- 本机 paraview_logs 中的启动命令改用时间、端口、唯一启动编号命名，避免同端口覆写；新增 JSON 参数记录。
- 每次启动的远端服务日志存入 `.tools/paraview-mpi/logs/launch-启动编号/`，JSON 中记录目录。复用服务脚本已有 PARAVIEW_SERVER_LOG_DIR 配置机制，无须另加日志实现。
- ParaView 客户端 stdout/stderr 分别写入本机文件；成功验证后保存完整桥接启动回执。客户端流日志不保证涵盖完整 GUI Output Messages，手动 Save to File 仍适用。
- 保留 cmd /k 的服务窗口与 SSH 隧道生命周期，保持已有版本/哈希/GUI 往返验证；不隐藏服务器登录提示。

验证：PowerShell tree-sitter 语法解析无 ERROR/missing 节点；桥接现有 49 项回归通过，10 项因环境条件跳过；git 按既有 CRLF 行尾检查无新增空白问题。当前节点无 pwsh，尝试下载官方便携版因连接超时失败，因此未运行原生 PowerShell 解析器或 Windows 实机启动验收。现有 GUI 未重启。用户下次使用时须重新下载新版 PS1 到 Windows；脚本不会更新本机自身。
