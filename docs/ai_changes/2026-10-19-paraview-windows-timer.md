# 修复 Windows ParaView 缺少 PythonQt 时的控制桥启动

- 目的：用户在 ParaView 5.11.2 内运行客户端确认缺少 PythonQt，旧客户端无法加载。
- 范围：仅控制桥客户端和使用说明；不修改用户连接脚本。
- 实现：Windows 复用标准库 ctypes 调用 SetTimer/KillTimer，替换该平台 Qt 定时器依赖；封装原生回调引用和停止生命周期，保留指针宽度 ID，检查主线程并防止重入。非 Windows 保留原 Qt 路径。Windows 后台 SSH 不弹出控制台窗口。
- 验证：仓库 .venv 语法解析、模拟 Win32 API 下的主线程回调、64 位 ID、重入保护、重复停止、停止后回调忽略、引用保留及后台线程创建拒绝通过；文档路径检查和本次暂存 diff 检查通过。
- 边界：当前 Linux 环境无法真实验证 Windows 消息分发；需用户重新下载并在 ParaView 内加载后完成 SSH 和 GUI 联调。该实现依据 Microsoft SetTimer 文档，不宣称端到端已通过。
