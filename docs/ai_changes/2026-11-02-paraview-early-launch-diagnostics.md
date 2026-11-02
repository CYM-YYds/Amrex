# 启动早期终端记录与保留窗口入口

证据：用户最新 `后处理脚本/pvserver_20261102_155050_port11116_8e25960601bc48e38033349d966ae7ae.bridge.zip` 只有成功启动编号 8e259606 的记录；stdout 包含注册、GUI 回执成功，随后反复 `Connection closed by 27.18.114.25 port 6022`。由此确认持续桥接传输也受 SSH 关闭影响，不能把 v23 初始化重试视为底层问题已修复。前两次启动的具体错误仍不在该包中。

日志缺口：原 pvserver 参数 JSON 在本机版本、SSH、端口等检查通过后创建；bridge.json 只在 GUI 回执验证成功后写入。前期失败不会产生这些文件；解析/策略错误发生在 PS1 运行前，脚本内部 trap 无法捕获。

实现：PS1 在首次版本/SSH 检查前开启独立 launcher_时间_启动编号.log transcript，同目录不可写时回退临时目录。try/finally 覆盖整个启动流程（包含状态查询 return），在结束时停止 transcript，避免记录调用者后续会话。pvserver 参数 JSON 引用本次 transcript。复用现有异常记录，不新增另一套错误归类。

增加长期 Windows connect_paraview.cmd（skill 内唯一实现，后处理脚本目录兼容软链），仅为 PS1 提供外层宿主：优先 pwsh，回退 powershell，使用 NoProfile/NoExit；PS 退出后仍 pause，不绕过执行策略，不启动另一套服务逻辑。该入口是必要的，因为 PS1 自身不能捕获自身解析前的错误。用户需将 CMD 和 PS1 下载到同一目录，推荐双击 CMD。

验证：PowerShell tree-sitter 语法通过；桥接回归通过。原生 PowerShell 测试扩展为验证冲突参数在 SSH 前失败时也产生 transcript；本机无 pwsh，原生测试仍跳过，CMD 双击与 Windows 实机启动留待现场验证。当前 GUI 未重启，原始压缩包保持不变。SSH 被入口关闭的底层原因仍需服务端证据，本次未推断为限流或修改服务器配置。
