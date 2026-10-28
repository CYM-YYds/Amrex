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
AMReX 单元速度在求导副本中暂时清除 `REFINEDCELL`，使被细层覆盖但仍存储的粗速度
参与本层差分；输出恢复原覆盖掩膜，输入不变。该标记由读取器生成，不等同于 PlotFile
导出 ghost。修复依赖被覆盖粗速度有效，不补充缺失的块间 ghost，也不构造跨层差分。
结果为物理坐标下未乘 H/U 的旋度；块边界与粗细离散误差仍需单独验证。
热更新桥脚本不会自动改写已有 Gradient 管线。需要新视图时使用 `add_vorticity_view`；
保留现有切片时使用下述 `repair_vorticity_slice`。

已有 `Calculator → Gradient → Slice` 管线可原位修复，保留切片与显示：

```bash
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current repair_vorticity_slice \
  --source Slice3 --timeout 600
```

`--source` 精确指定唯一切片名称；要求它直接连接原生 Gradient，且 Gradient 的输入为
三维单元速度向量。复用原速度数组和覆盖掩膜修复，只把该切片的 Input 接到新的求导过滤器；
原 Gradient 保留，切片平面、显示对象、相机与色标不重建。该操作只输出旋度，原 Gradient 的
其他派生量仍留在原管线上；已有非原生 Gradient 或二维速度输入会在修改连接前拒绝。
操作失败时恢复原输入。热更新后再使用；超时先查看同 ID 的回执，不重复投递。
已修复的切片不再直接连接原生 Gradient，无需再次执行修复。保存新的 `.pvsm` 状态文件
才能在重开场景后保留该连接；加载修复前的旧状态仍会使用原管线。

涡量切片的论文/参考图排版（`publication_vorticity`）与独立 Z 轴标题视图
（`add_axis_title_view`）见 [涡量切片出图](publication.md)。

桥仅支持 inspect 声明的操作，不接受任意 Python。所需能力未实现时，独立任务可转用
批处理；必须留在当前窗口的操作按 [维护说明](maintenance.md#实现边界与验证) 扩展工具。

## 用户自行生成修复节点

下载 [repair_amr_gradient.py](../scripts/repair_amr_gradient.py) 为 Windows 普通文件即可，
运行时不需要桥接、仓库路径或第二个脚本。它是用户直接操作的长期入口，不是一次性诊断。

1. 在三维数据上创建普通 Gradient，选择 **Cell Data** 数组，勾选需要的 Gradient、Vorticity、
   Divergence 或 Q Criterion 并 Apply。
2. 在 Pipeline Browser 选中该 Gradient，打开 **View → Python Shell**，用 **Run Script**
   加载下载文件；也可执行（替换实际文件路径）：

   ```python
   exec(open(r'D:\OneDrive\Desktop\repair_amr_gradient.py', encoding='utf-8').read())
   ```

3. 新节点例如 `Gradient1_AMR_fixed` 会被选中，后续自行添加 Slice、Contour 等。

新节点复用原 Gradient 的输入数组、输出勾选项及数组名，解除覆盖后重新计算，再恢复掩膜。
它与旧 Gradient 并列连接三维输入，旧节点及已有切片不变；脚本不自动显示数据或改相机、色标。
修复节点的类型为 Programmable Filter；如需调整计算勾选项，修改原 Gradient 后重新生成。
重复执行生成唯一新名称。当前范围为三维单元数据，细块缺失 ghost 的限制同前。
脚本需在 ParaView 的 Python Shell 执行，不能用普通系统 Python。保存新的 `.pvsm` 可保留结果。

## 故障处理与验证边界

- 服务端断线：启动器自动将服务 stdout/stderr 同时写入共享仓库
  `.tools/paraview-mpi/logs/pvserver_日期时间_随机后缀.log`，路径也显示在启动窗口中；
  关闭 Windows 窗口后日志仍保留。版本预检不生成日志，日志不自动清理。
  已使用当前远端启动器的 PS1 无需因该日志功能重新下载。该日志不包含 Windows 客户端
  Output Messages、SSH 客户端错误或自动调用栈；客户端窗口用 `Save to File…` 单独保存。
  `PARAVIEW_SERVER_LOG_DIR` 可覆盖远端日志目录。退出码 0 不代表此前没有协议错误。
  已观察到的 `Tag mismatch`（188968 为取消检查标签）仍未确定根因，不能把涡量修复或
  Linux 客户端层级切换通过当作 Windows GUI 断线已修复；需保留首次错误和触发操作。
- PS1 窗口立即关闭：在已打开的 PowerShell 中用 `pwsh -NoExit -File "本机实际路径/connect_paraview.ps1"` 保留完整报错。
- 版本预检得到空值：重新下载 PS1；当前实现读 EXE 版本资源，缺失时用同目录 pvpython，不能依赖 GUI `paraview.exe --version` 的管道输出。
- PythonQt 缺失：确认使用最新桥客户端，且在 ParaView GUI 内加载，而非普通 Python。
- 只有 timer/registered 输出：检查 GUI、SSH stderr 与启动回执；注册或新鲜心跳不代表 GUI 已响应。
- 超时、源码哈希或 URI 不匹配：保留窗口与诊断文件，先定位连接、客户端或传输层；不自动关闭用户窗口。
- SSH 指纹或认证失败：核验目标和认证，不关闭主机验证来绕过。

当前操作是否成功以对应命令回传为准；远端测试不能代替 Windows 本机的完整启动验收。
