# ParaView 当前窗口控制桥

此桥独立于 [connect_paraview.ps1](connect_paraview.ps1)，不修改其连接和用户配置。
仅允许 `inspect`、`background`、`restore_background`；不接受任意 Python 代码。

## 本机接入

1. 保持现有 ParaView 与超算连接。
2. 将 [paraview_bridge_client.py](paraview_bridge_client.py) 下载到本机，例如
   `D:/Software/paraview_bridge_client.py`。
3. 本机终端先确认以下命令无需密码即可运行（先完成主机指纹确认和 SSH 密钥配置）：

   ```powershell
   ssh -T -o BatchMode=yes -p 6022 Liuzhaohui@27.18.114.25 "hostname"
   ```

4. 打开 ParaView 的 Python Shell，执行（替换为实际本机路径）：

   ```python
   exec(compile(open(r'D:/Software/paraview_bridge_client.py', encoding='utf-8').read(), 'paraview_bridge_client.py', 'exec'))
   ```

5. 将打印的 `ParaView bridge ready: /tmp/paraview-bridge-...` 整行发给 Agent。
   该目录位于 SSH 目标主机；`/tmp` 通常不跨节点共享。若 Agent 不在该主机，
   需在该 SSH 主机执行投递器，不能将当前节点的同名目录视为同一会话。

Windows 客户端使用标准库 ctypes 调用 SetTimer，不依赖 PythonQt，也不需要 pip 安装。
非 Windows 客户端仍要求 ParaView 提供 PythonQt。请勿用 VS Code 的普通 Python 启动。
加载失败时继续使用原有 ParaView 连接即可。

## 超算端投递

从仓库根运行，下面的会话目录须替换为真实值。严格串行投递；此工具供单个控制者使用。

```bash
.venv/bin/python 后处理脚本/paraview_bridge_send.py /tmp/paraview-bridge-会话标识 inspect
.venv/bin/python 后处理脚本/paraview_bridge_send.py /tmp/paraview-bridge-会话标识 background --color 0.15 0.20 0.30
.venv/bin/python 后处理脚本/paraview_bridge_send.py /tmp/paraview-bridge-会话标识 restore_background
```

选择一个渲染视图后再执行背景操作。每个视图第一次修改前保存其背景颜色和模式，
恢复成功后清除备份。恢复作用于当前活动视图。
此验证不加载、删除或修改数据管线，不代表数值结果验证。

命令通过临时文件加重命名发布；结果带命令 ID。客户端会话内对相同 ID 去重，
传输失败时只重试结果回传。超时不代表命令取消，须先检查 `result.json`，
不可盲目重发。客户端输出错误用于诊断，后台 SSH 最长等待 15 秒。

## 停止

先恢复需要恢复的视图背景，再在本机 Python Shell 执行：

```python
pv_bridge.stop()
```

停止不会删除远端会话目录，也不会断开原有 `pvserver`。重新加载脚本会停止旧桥并
创建新会话；旧背景备份不会迁移，应先恢复。后台正在执行的 SSH 最长需 15 秒退出。
同账户有权限写会话目录的进程可以请求上述操作，目录以 0700 权限创建；不新增网络监听端口。

## 验证边界

已做 Python 语法、模拟 ParaView/Qt 下的命令执行与恢复、错误路径、文件投递/回传验证。
Windows 原生定时器与当前 ParaView 5.11.2 GUI 联调须用户重新加载后验证。
SetTimer 回调依赖 GUI 消息循环，执行长任务时会延迟；回调带主线程检查和重入保护。
依据 [Microsoft SetTimer 文档](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-settimer)。
ParaView 客户端脚本入口见 [官方 5.11 教程](https://docs.paraview.org/en/v5.11.0/Tutorials/ClassroomTutorials/pythonAndBatchParaViewAndPython.html)。
