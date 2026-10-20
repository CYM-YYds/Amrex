# ParaView 当前窗口控制桥

## 日常启动与节点前提

将 [connect_paraview.ps1](../scripts/connect_paraview.ps1) 下载为本机普通文件，例如
`D:/OneDrive/Desktop/connect_paraview.ps1`，在 Windows PowerShell 运行：

```powershell
& 'D:\OneDrive\Desktop\connect_paraview.ps1'
```

需要本机 OpenSSH、已核验的主机指纹和免密认证。本机 ParaView 默认路径为
`D:\Software\ParaView 6.2.0\bin\paraview.exe`。脚本检查客户端和服务端均为 **6.2.0**，
版本不匹配时停止，不自动选用旧版。本机从 EXE 版本资源读取版本；若资源缺失，
使用同目录 pvpython.exe 的控制台版本输出。Windows GUI 的 paraview.exe --version 可能无管道输出，不能用空输出来判断版本不符。
远端使用仓库 `.tools/paraview-batch/env/bin/pvserver`，不再加载系统 ParaView/MPI 模块。
此构建的 `PARAVIEW_USE_MPI=OFF`，因此使用单进程；不能改成 mpirun -np 32。

SSH 用户、外部入口和端口在 PS1 配置区维护，并通过环境变量传给桥客户端。
服务命令限定 **whshare-agent-1**：外部 SSH 入口已在该节点时直接运行并使用回环隧道；
入口在其他节点时才转发 SSH，隧道目的地相应为编译节点。避免编译节点 SSH 回连自身受限。
[服务包装脚本](../scripts/run_paraview_server.sh) 再检查主机名，不符合即退出。
外部入口到编译节点的 SSH 须具备已核验的指纹和免密认证；否则启动预检失败，不回退登录节点。
桥的文件轮询仍走外部 SSH 入口，共享注册目录保持不变。

升级时把新版 PS1 重新下载到本机；旧本机 PS1 不会自动替换自身。先保存现有工作，关闭占用
11115 的旧隧道后运行新版；脚本不会自动终止旧窗口或服务。客户端仍会在每次启动时下载。

启动过程为：

1. 检查本机固定端口 11115；被占用即停止，避免误连旧服务。
2. SCP 下载仓库最新桥客户端到 PS1 同目录；失败停止，保留旧文件。
3. 在编译节点选择 11115 起的空闲端口，建立指向该节点的 SSH 隧道并启动单进程 pvserver；保留服务输出窗口。
4. 等待远端监听后，启动 GUI，传入 `--url=cs://localhost:11115` 与 `--script`。
5. GUI 主线程执行启动 inspect，后台发布本次启动专属回执。PS1 核对启动编号、桥版本、
   本机文件 SHA256、所需能力和实际连接 URI；全部匹配且 GUI 未退出才显示 `Bridge ready`。

启动回执证明该次检查成功；后续可用性仍以对应命令回传为准。它不重建上次管线，
也不自动加载数据。桥版本3的白名单为 inspect、background、restore_background、export_centerline、add_vorticity_view。
既有窗口不会随仓库更新自动升级；不要手动另建工作线程或清除 closed 事件来“重启”旧实例。

## 固定位置与状态所有者

| 用途 | 位置或规则 |
|---|---|
| 本机客户端 | PS1 同目录的 paraview_bridge_client.py，每次启动覆盖下载 |
| 仓库唯一实现 | 本 skill 的 scripts/paraview_bridge_client.py |
| 当前入口 | `/home/share/iosoeqkp/home/Liuzhaohui/.cache/paraview-bridge/current.json` |
| 命令、结果、心跳 | 上述根目录下各自独立的 session-UUID 目录 |
| 本机数据连接 | localhost:11115，隧道转发到选定远端端口 |

新会话注册时原子更新 current.json，旧会话心跳不会覆盖它；多个窗口时指向最后注册者，
不自动退回旧窗口。传输线程更新心跳；超过 90 秒时自动发现拒绝投递，心跳新鲜不保证 GUI 可响应。
launch-UUID.json 仅在 GUI 执行本次启动查询后发布。停止桥不删除诊断文件，也不关闭 pvserver。

## Agent 投递与批量处理

从仓库根使用 `.venv/bin/python`：

```bash
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current inspect
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current background --color 0.15 0.20 0.30
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current restore_background
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current export_centerline \
  --input /absolute/shared/plotfile --output /absolute/shared/existing-directory/profile.csv \
  --resolution 1024 --max-level 2 --timeout 180
```

可省略 current。单个控制者严格串行投递；上条命令未回传时拒绝覆盖。
等待超时不代表取消，先检查对应 ID 的 result.json，不能盲目重发。
批量处理逐个输入使用独立输出，逐个等待结果。桥在线时超算端可自动投递，不要求用户逐条操作 GUI。

中心线导出复用 AMReXBoxLibGridReader → ExtractBlock /Root → CellDataToPointData → PlotOverLine，
按三维 bounds 几何中心沿 z 采样 ux、uy、uz，默认请求 level 2、1024 段、17 位 CSV 精度。
输出父目录须已存在。成功时保留新管线、恢复原活动源，不主动改变显示；失败只回收本次创建的对象。
分析端须检查有效掩码和有限值，不能把默认请求层级当作输入实际层级的证明。

## 涡量视图

先inspect核对读取器名称；`--source`按完整注册名称精确匹配，不展开通配符。
应选择原始三维速度读取器，不能在已有二维Slice上计算三维旋度。
命令保留现有管线、相机和布局，在新建的Vorticity布局中显示切片：

```bash
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current add_vorticity_view \
  --source 'plt_003200*' --slice-axis y --component Magnitude --timeout 180
```

上述读取器名称只是例子，必须使用当前inspect查到的名称及输入路径。
`--slice-axis x/y/z`选择法向，`--slice-position`指定物理位置（默认几何中心），
`--component Magnitude/X/Y/Z`选择旋度模或分量。当前时间沿用原活动视图，不跳转到别的输出。
结果回传输入、时间、几何、旋度范围和新增对象ID；显示范围在GUI颜色图编辑器中调整。

管线为三维输入 →（AMReX时ExtractBlock /Root）→ 必要的CellDataToPointData →
Calculator组合`ux*iHat + uy*jHat + uz*kHat` → Gradient计算`Vorticity=curl(u)` → Slice。
`Vorticity_3D`保留三分量结果，可以在GUI中继续添加其他切片或显示方式。
涡量是物理坐标下的有量纲旋度，未乘H/U；AMR界面处的点重建和分块导数属于可视化近似，
不能直接当作求解器离散涡量、守恒指标或严格数值验证。

旧窗口inspect若缺少add_vorticity_view，不会随仓库文件更新自动获得此功能。
可在GUI Python Shell中更新当前本机客户端并重载，无需关闭数据连接：

```python
import builtins, os
from pathlib import Path
bridge_script = Path(os.environ['PV_BRIDGE_SCRIPT'])
bridge_script.write_bytes(builtins._amrex_paraview_bridge_runtime.bridge.remote('cat /home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/.agents/skills/paraview/scripts/paraview_bridge_client.py'))
exec(compile(bridge_script.read_text(encoding='utf-8'), str(bridge_script), 'exec'))
```

这要求当前窗口已按PS1流程加载桥且PV_BRIDGE_SCRIPT有效。若此前桥修改过背景，先恢复背景再重载。
代码写回实际本机文件，启动回执哈希对应真实执行源码；不是通过桥命令执行任意代码。
重载后等待新会话inspect确认版本3和新增能力，再投递涡量命令；不要盲目向旧会话重试。
以后重新启动时，本机PS1也需下载新版，其启动预检要求版本3与五项能力。

## 已有窗口手动加载、升级与停止

优先使用 PS1 启动。需保留已有数据连接时，先把最新桥客户端下载到本机，再在
**ParaView GUI Python Shell** 执行（路径以本机为准）：

```python
import os
bridge_script = r'D:/OneDrive/Desktop/paraview_bridge_client.py'
os.environ['PV_BRIDGE_SCRIPT'] = bridge_script
exec(compile(open(bridge_script, encoding='utf-8').read(), bridge_script, 'exec'))
```

先恢复由桥修改的背景再重新加载，以免丢失原背景备份。手动加载必须让 PV_BRIDGE_SCRIPT
指向真正读取的文件；仅 exec 远端源码而保留旧本机文件，不能用旧文件哈希证明运行源码身份。
手动加载不等于 PS1 整套启动流程已验证。无需执行 from paraview.simple import *，更不能把输出提示当作代码粘贴。

启动脚本与交互 Shell 的全局上下文不同。查看或停止实例先执行：

```python
import builtins
pv_bridge = builtins._amrex_paraview_bridge_runtime.bridge
# 需要停止控制桥时执行；原 pvserver 连接仍保留。
pv_bridge.stop()
```

builtins 不存在表示该窗口尚未加载客户端，应先用本机文件加载，不能先调用 pv_bridge.remote。
重载会停止已知旧实例并等待其工作线程退出，最长约 50 秒；未退出时报错，不创建第二个所有者。
原生回调保存在应用级运行时，避免重载时被回收。Windows 使用 ctypes SetTimer，不需要 PythonQt；
ParaView API 在 GUI 主线程执行，普通 SSH 轮询在后台。SSH 单次超时 45 秒；非 Windows 保留 PythonQt 路径。

## 故障处理与验证边界

- PS1 窗口立即关闭：从已打开的 PowerShell 运行 `pwsh -NoExit -File "D:\OneDrive\Desktop\connect_paraview.ps1"`（替换为本机真实路径），保留错误位置和完整文字。
- Expected local ParaView 6.2.0; got: 空值：旧版 PS1 错将 GUI 的空版本输出视作不匹配；重新下载当前 PS1。当前版本优先读文件资源、缺失时使用 pvpython.exe，仍会拒绝真实版本不符。
- PythonQt 缺失：确认加载的是新 Windows 客户端，且在 ParaView GUI 内执行，不能用 VS Code 普通 Python。
- 只有 timer/registered 输出：不代表 GUI 回执成功，检查输出窗口、SSH stderr 和本次 launch 回执。
- 超时、哈希/版本或 URI 不匹配：保留窗口及诊断文件，核对本机文件和配置后重新加载或启动；不自动关闭用户窗口。
- SSH 指纹或认证错误：核验目标和认证，不通过关闭主机验证来绕过。

2026-10-19 的证据分为：

- 早期 5.11 Windows 现场覆盖原生计时器、SSH 回传、管线查询、背景修改和中心线提取。
- 6.2.0 在编译节点的 pvpython → pvserver 实测覆盖连接、320×240 单张离屏 Sphere 截图和真实 PlotFile 的 1025 点中心线导出。导出 9 列数值与 batch 基线一致。无 X 显示的探测警告仍出现，但截图和导出成功；动画、Windows GUI 截图和 MPI 并行未验证。
- 升级及版本预检修复后，新的在线会话已实际 inspect 成功；本次回执 ID、URI、四项能力及桥客户端源码 SHA256 均符合当前仓库。实际服务进程为编译节点独立环境的 pvserver，监听 11115，当前管线已有读取器和两个 Slice。
- 仓库门禁覆盖启动协议、跨上下文生命周期、节点路由和 PowerShell 语法树。尚未直接获取用户 Windows 的最终 Bridge ready 控制台输出或 PE 版本资源值，不能把远端证据扩张为所有本机启动分支均已验收。

现场状态快照和清理候选见 [知识收尾记录](../../../../docs/ai_changes/2026-10-19-paraview-knowledge-closeout.md)。

## 迁移兼容

旧 `后处理脚本/connect_paraview.ps1` 与 `后处理脚本/paraview_bridge_client.py` 为指向 skill 实现的软链，
保留供已下载的旧 PS1 获取客户端。下载到 Windows 时使用普通文件，不把软链文本当作脚本。
其他命令使用本文新路径；兼容入口不是重复实现或可随意删除的备份。
