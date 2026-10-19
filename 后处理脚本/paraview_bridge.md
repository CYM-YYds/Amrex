# ParaView 当前窗口控制桥

## 日常启动

首次将最新版 [connect_paraview.ps1](connect_paraview.ps1) 下载到本机，例如
`D:/OneDrive/Desktop/connect_paraview.ps1`（以本机真实目录为准）。此后直接运行该启动脚本：

```powershell
& 'D:\OneDrive\Desktop\connect_paraview.ps1'
```

脚本会完成以下步骤：

1. 检查本机 ParaView 路径及固定隧道端口 `11115`，已占用则停止，避免误连旧服务。
2. 通过 SCP 从超算仓库同步最新版 `paraview_bridge_client.py` 到启动脚本同目录。
   下载失败则停止，保留旧文件；不要在该自动更新文件中维护本机定制。
3. 启动 SSH 隧道及远端 32 进程 pvserver，保留可查看错误的服务窗口。
4. 等待远端监听端口就绪，再用 `--url=cs://localhost:11115` 自动连接，
   并用 `--script=本机脚本路径` 自动加载控制桥。
5. 控制桥发布固定的共享目录入口；无需手动执行 Python Shell，也无需复制随机会话路径。

需要 Windows OpenSSH 的 ssh/scp 可用，已确认主机指纹并配置密钥免密登录。
本机 ParaView 默认路径是 `D:\Software\ParaView 5.11.2\bin\paraview.exe`，
找不到时仍按原逻辑搜索安装目录。服务器使用现有模块 ParaView 5.11.1。
SSH 用户、主机和端口统一在启动脚本配置区维护，再通过子进程环境传给控制桥。
首次更新启动流程后仍需 Windows 现场验证自动连接与加载。

脚本建立连接和控制入口，不自动重建上次的数据管线。正常打开远端数据或加载状态后，
Agent 可读取当前管线。当前命令只支持读取管线及背景切换/恢复，不代表已开放任意数据处理。
开启多个桥时，固定入口指向最后成功注册的会话；不自动退回旧会话。

## 固定连接位置

| 用途 | 固定位置 |
|---|---|
| 本机控制桥 | `connect_paraview.ps1` 同目录的 `paraview_bridge_client.py` |
| 超算控制桥源码 | `/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/后处理脚本/paraview_bridge_client.py` |
| 超算当前会话入口 | `/home/share/iosoeqkp/home/Liuzhaohui/.cache/paraview-bridge/current.json` |
| 本机数据连接 | `localhost:11115`，远端端口从 11115 起自动选择 |

每次启动仍创建独立 `session-<UUID>` 目录，防止旧命令重放；`current.json` 原子更新，
指向最新会话。会话目录保存命令、结果和心跳，位于共享家目录，跨登录节点可见。
心跳由传输线程更新，超过 90 秒时自动发现拒绝投递；心跳不保证 GUI 未阻塞，
实际控制成功以对应 ID 的回传为准。停止后不会删除诊断文件。

## Agent 投递

在仓库根目录使用固定入口，无需先 SSH 到 `whshare-agent-1`：

```bash
.venv/bin/python 后处理脚本/paraview_bridge_send.py current inspect
.venv/bin/python 后处理脚本/paraview_bridge_send.py current background --color 0.15 0.20 0.30
.venv/bin/python 后处理脚本/paraview_bridge_send.py current restore_background
```

也可省略 `current`，例如 `.venv/bin/python 后处理脚本/paraview_bridge_send.py inspect`。
严格串行投递，工具供单个控制者使用。上条命令未回传时拒绝覆盖；等待超时不代表取消，
先查看对应会话的 `result.json`，不能盲目重发。

## 手动加载与停止

已有 ParaView 窗口也可在 Python Shell 加载下载后的脚本：

```python
exec(compile(open(r'D:/OneDrive/Desktop/paraview_bridge_client.py', encoding='utf-8').read(), 'paraview_bridge_client.py', 'exec'))
```

Windows 使用标准库 ctypes 的 SetTimer，在 GUI 消息循环中处理命令；不依赖 PythonQt。
非 Windows 保留 PythonQt 路径。请勿使用 VS Code 的普通 Python 启动客户端。
SSH 调用只在后台线程运行，超时为 45 秒；后台失败会打印 stderr 用于排查。

背景操作只作用于活动渲染视图，并保存原始 RGB 和背景模式整数。
重新加载前先恢复背景，再执行 `pv_bridge.stop()`，以免丢失旧备份。
停止不会断开原有 pvserver；后台已发出的 SSH 最长需 45 秒退出，最后心跳会随后过期。
关闭旧 SSH 服务窗口后再启动新连接，固定本机端口不允许并行使用。

## 验证边界与依据

此前 Windows 现场已验证原生定时器、SSH 回传、读取管线及修改背景；旧背景模式字符串恢复
失败后通过 Python Shell 恢复。当前整数枚举恢复、自动下载/启动和固定入口仍需新版现场联调。
仓库内检查包括 Python 语法、模拟 GUI 命令、真实文件协议往返、会话发现、心跳失效及路径检查。

ParaView 5.11.2 [启动处理源码](https://github.com/Kitware/ParaView/blob/v5.11.2/Qt/ApplicationComponents/pqCommandLineOptionsBehavior.cxx)
先处理服务器连接，再执行 Python 脚本；定时器依据
[Microsoft SetTimer 文档](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-settimer)。
