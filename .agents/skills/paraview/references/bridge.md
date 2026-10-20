# GUI 桥接

用于操作用户当前 Windows ParaView 窗口；独立远端处理见 [batch.md](batch.md)。

## 本机启动

将 [connect_paraview.ps1](../scripts/connect_paraview.ps1) 下载为 Windows 普通文件，
在本机 PowerShell 运行（路径替换为实际保存位置）：

```powershell
& 'D:\OneDrive\Desktop\connect_paraview.ps1'
```

本机需 OpenSSH、已核验的主机指纹和免密认证。默认 GUI 路径为
`D:\Software\ParaView 6.2.0\bin\paraview.exe`，客户端和服务端须同版本。
SSH 账号、入口和本机路径在 PS1 配置区维护；远端安装与执行节点见 [主入口](../SKILL.md#环境与执行方式)。

PS1 下载桥客户端，在编译节点启动 pvserver 并建立 SSH 隧道，再打开 GUI。
外部 SSH 入口已在编译节点时直接启动，其他入口则再跳转；本机连接为 `localhost:11115`。
启动编号、客户端源码和实际连接的回执匹配后显示 `Bridge ready`。
它不重建上次管线、不自动加载数据。端口被占用时先检查已有窗口和隧道，避免重复启动。

默认服务为 4 进程，可在新建连接时配置：

```powershell
& 'D:\OneDrive\Desktop\connect_paraview.ps1' -ServerProcesses 4 -RemoteParaViewPrefix '/absolute/path/to/paraview-mpi/env'
```

`ServerProcesses` 为 1–256；省略 `RemoteParaViewPrefix` 使用仓库默认安装。
自定义前缀须为不含空格或 Shell 元字符的绝对路径，包含同版本 pvserver 和配套 mpiexec。
不能混用系统 MPI；已有连接改变进程数需先保存场景，再新建服务连接，热更新不能改变 rank 数。

## 已有窗口：查询与更新

从仓库根查询实际连接、数据源、视图和可用能力：

```bash
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current inspect
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current client_status
```

| 状态 | 下一步 |
|---|---|
| `ready` | 按 inspect 返回的能力执行 |
| `client_update_required` | 连接仍正常；需要新能力且 `can_hot_update` 为真时执行 reload_client |
| `disconnected` | 桥可响应，但需检查 GUI 的数据连接 |
| `bridge_unreachable` | 检查窗口是否在线、SSH、心跳和命令回传 |

```bash
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current reload_client --timeout 180
```

热更新只加载固定仓库来源，保留连接、管线及视图。更新后重新查询状态；不支持热更新的
旧客户端可继续现有操作，需要新能力时见 [手动加载](maintenance.md#已有窗口手动加载与停止)。
Windows 端也可运行 PS1 的 `-BridgeStatus` 或 `-UpdateBridge`，均不创建服务、隧道或 GUI。
PS1 本身更新后须重新下载，它不会更新自身；正常启动时会下载最新桥客户端。

`current` 指向最后注册的窗口，不自动退回旧会话。多个窗口时先核对 inspect 中的输入和 URI，
或使用客户端打印的完整会话目录替代 `current`。命令须串行；超时不代表取消，先在
`/home/share/iosoeqkp/home/Liuzhaohui/.cache/paraview-bridge/` 对应 `session-UUID` 中
核对 `command.json` 与同 ID 的 `result.json`，不能盲目重发。

## 常用操作

```bash
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current background --color 0.15 0.20 0.30
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current restore_background
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current export_centerline \
  --input /absolute/shared/plotfile --output /absolute/shared/output/profile.csv \
  --resolution 1024 --max-level 2 --timeout 180
```

中心线的采样定义与输出检查见 [批处理中心线说明](batch.md#现有中心线导出)，两种入口共用算法。
GUI 导出成功时保留新管线，恢复原活动源，不主动改变显示；失败只回收本次创建的对象。

添加涡量视图时，先从 inspect 查到原始三维读取器的完整注册名称及输入路径，再运行：

```bash
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current add_vorticity_view \
  --source '<inspect返回的完整名称>' --slice-axis y --component Magnitude --timeout 180
```

`--source` 精确匹配，不展开通配符。`--slice-axis x/y/z` 选择法向；`--slice-position` 默认
几何中心；`--component Magnitude/X/Y/Z` 选择模或分量；`--color-range MIN MAX` 指定色标范围。
命令先在原始三维速度上求旋度，再切片，在新布局显示，保留原管线、视图与相机。
优先使用原生单元速度，只有点速度时走点数据路径；回执注明关联、数值范围及新增对象 ID。
结果为物理坐标下未乘 H/U 的旋度；AMR 界面重建与分块导数是可视化近似。

已有涡量切片可使用 `publication_vorticity` 原位排版：

```bash
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current publication_vorticity \
  --source '<inspect返回的涡量切片完整名称>' --vorticity-scale 20 \
  --color-range 0 10 --output /absolute/shared/output/vorticity.png --timeout 180
```

该操作支持坐标轴对齐的平面切片；要求其在唯一视图中可见，或已选中目标视图。
复用已有 `Vorticity`，新增标量 `mag(Vorticity)*H/Uref`，隐藏原切片显示而保留管线。
乘数和色标须由实际算例确定，上例 20 仅适用于 H=1、Uref=0.05；默认乘数 1
表示未归一化。原始单元/点关联保持不变，不做平滑。白底、Times 字体、水平色标与
蓝青绿黄红色带固定；平面内坐标范围为 0–1 时轴标题使用 x/H 等，否则保留原坐标。
超过色标上限的值饱和显示，比较图须使用相同定义和范围。
可选 `--output` 导出 2400×2400 无损 PNG 到服务端共享路径，父目录需预先存在；
不指定输出则只修改 GUI。重复执行复用同一切片的论文标量过滤器。

桥仅支持 inspect 声明的操作，不接受任意 Python。所需能力未实现时，独立任务可转用
批处理；必须留在当前窗口的操作按 [维护说明](maintenance.md#实现边界与验证) 扩展工具。

## 故障处理与验证边界

- PS1 窗口立即关闭：在已打开的 PowerShell 中用 `pwsh -NoExit -File "本机实际路径/connect_paraview.ps1"` 保留完整报错。
- 版本预检得到空值：重新下载 PS1；当前实现读 EXE 版本资源，缺失时用同目录 pvpython，不能依赖 GUI `paraview.exe --version` 的管道输出。
- PythonQt 缺失：确认使用最新桥客户端，且在 ParaView GUI 内加载，而非普通 Python。
- 只有 timer/registered 输出：检查 GUI、SSH stderr 与启动回执；注册或新鲜心跳不代表 GUI 已响应。
- 超时、源码哈希或 URI 不匹配：保留窗口与诊断文件，先定位连接、客户端或传输层；不自动关闭用户窗口。
- SSH 指纹或认证失败：核验目标和认证，不关闭主机验证来绕过。

当前操作是否成功以对应命令回传为准；远端测试不能代替 Windows 本机的完整启动验收。
历史验证按日期查 [6.2.0 收尾记录](../../../../docs/ai_changes/2026-10-19-paraview-knowledge-closeout.md)
和后续 [MPI 验收记录](../../../../docs/ai_changes/2026-10-20-paraview-mpi-install.md)，不把其中会话快照当作当前在线状态。
