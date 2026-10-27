# 实现与维护

仅在安装环境、修改工具或处理旧客户端时阅读。日常操作见 [主入口](../SKILL.md)。

## 安装环境

现有安装缺失或需修复时，从仓库根运行：

```bash
bash .agents/skills/paraview/scripts/install_paraview_batch.sh
```

安装器自动 SSH 到编译节点，不回退到登录节点。安装在 `.tools/paraview-mpi/env`，
不修改系统模块或 Shell 启动文件。集群另有 `/home/HPCBase/tools/ParaView-EGL-5.11.1`，
MPI 开启但 Python 关闭，不能替代此环境的 Python 桥接与批处理，也不能与 6.2.0 客户端混用。
依赖由 [MPI 锁文件](../scripts/paraview-mpi-linux-aarch64.lock) 固定；ParaView 官方源码
及引导程序的版本、SHA256 以 [安装器](../scripts/install_paraview_batch.sh) 为准。
安装状态来自 conda-meta 和实际应用探测，不另设缓存标记。

源码与目标文件在编译节点本地盘构建，共享目录保留安装、源码归档、缓存和日志。
`PARAVIEW_BUILD_JOBS` 可调整编译并发（默认 32），`PARAVIEW_BUILD_ROOT` 指定本地构建目录。
安装器加锁，匹配的依赖和 MPI 应用不重复构建。环境替换与数值差异的既有证据见
[MPI 安装记录](../../../../docs/ai_changes/2026-10-20-paraview-mpi-install.md)。

## 已有窗口手动加载与停止

只在旧客户端不支持热更新或当前窗口尚未加载桥时使用。先下载最新
[桥客户端](../scripts/paraview_bridge_client.py) 为本机普通文件，再在 **ParaView GUI Python Shell** 执行：

```python
import os
bridge_script = r'D:/OneDrive/Desktop/paraview_bridge_client.py'
os.environ['PV_BRIDGE_SCRIPT'] = bridge_script
exec(compile(open(bridge_script, encoding='utf-8').read(), bridge_script, 'exec'))
```

路径须指向实际执行文件。桥曾修改背景时先恢复背景，避免重载丢失备份。
不要只执行远端源码却保留旧本机文件，再用旧文件哈希证明运行源码身份。
加载后从远端 inspect 核对连接和能力；无需因此关闭数据连接。

需要停止桥时，在 GUI Python Shell 中访问应用级实例：

```python
import builtins
pv_bridge = builtins._amrex_paraview_bridge_runtime.bridge
pv_bridge.stop()
```

停止桥不关闭 pvserver。builtins 中没有该对象表示此窗口尚未加载客户端。

## 实现边界与验证

- 唯一实现位于本 skill 的 `scripts/`；`后处理脚本/connect_paraview.ps1` 和 `paraview_bridge_client.py` 是兼容软链。Windows 下载应得到普通脚本文件，保留旧入口兼容性。
- 桥客户端在 GUI 主线程调用 ParaView API，后台只处理 SSH；builtins 持有唯一实例与原生回调。重载须先结束旧线程，不创建多个所有者，不清除 closed 事件来重启旧实例。
- `run_paraview_server.sh` 检查执行节点和 MPI 能力，使用同一安装的启动器；版本探测不启动多 rank 服务。PS1 的版本预检使用 EXE 资源或 pvpython，启动能力从客户端 manifest 读取，不重复维护能力常量。
- `paraview_batch_export.py` 只加载桥模块定义，复用 `export_centerline`，不启动计时器、SSH 或会话注册。修改中心线算法须维护两种入口的共同实现。
- 热更新只接收固定仓库源码及匹配 SHA256，在 GUI 主线程替换命令实现，保留连接、视图及状态；传输、计时器或状态协议不兼容时提高 `RUNTIME_UPDATE_PROTOCOL`，要求正常启动。热更新后的状态以实时 inspect 为准，不重写旧启动回执。
- 新增桥操作先核对已有能力和 ParaView API，再实现并更新能力声明；不以任意 Python 投递绕过协议。涡量须在三维速度上计算后再切片，单元速度优先，兼容旧读取器时才 ExtractBlock。
- AMR 单元涡量修复的唯一求导实现为客户端的 `AMR_CELL_VORTICITY_SCRIPT`，新视图与现有
  切片操作共同复用。具体掩膜处理、限制和操作见 [GUI 桥接](bridge.md)。
- 用户单文件 `scripts/repair_amr_gradient.py` 由该算法与 `templates/repair_amr_gradient.py.in`
  生成，`后处理脚本/repair_amr_gradient.py` 为兼容软链。它独立分发，不依赖运行时桥接；
  设置区继承所选 Gradient 的输出选项，不维护第二套掩膜差分算法。改源后从仓库根重新生成：

  ```bash
  .venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current export_gradient_script \
    --output .agents/skills/paraview/scripts/repair_amr_gradient.py
  ```

  生成物同步回归会比较完整文件；不要直接手改生成物。

验证按修改范围选择：纯文档检查路径、链接和 skill 格式；修改执行机制时检查相应语言语法，
并运行现有回归：

```bash
bash -n .agents/skills/paraview/scripts/run_paraview_batch.sh
bash -n .agents/skills/paraview/scripts/install_paraview_batch.sh
bash -n .agents/skills/paraview/scripts/run_paraview_server.sh
.venv/bin/python -m unittest discover -s .agents/skills/paraview/scripts/tests -v
```

模拟回归覆盖启动协议、生命周期、热更新和节点路由，不能替代 Windows 现场运行。
修改数据管线需在编译节点用真实 PlotFile 检查输出；修改旋度还需解析旋转场回归、
Gradient 输出关联与有限值检查。明确实际覆盖的 AMR 层级、GUI/MPI 组合，不能凭编译、
截图或一次导出成功宣称数值等价。
