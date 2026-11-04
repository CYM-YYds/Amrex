# GUI 桥接

用于操作用户当前 Windows ParaView 窗口；独立远端处理见 [batch.md](batch.md)。

## 本机启动

将 [connect_paraview.ps1](../scripts/connect_paraview.ps1) 下载为 Windows 普通文件，
在本机 PowerShell 运行（路径替换为实际保存位置）：

```powershell
& 'D:\OneDrive\Desktop\connect_paraview.ps1'
```

若通过双击启动，建议同时下载 [connect_paraview.cmd](../scripts/connect_paraview.cmd) 与上述 PS1，
放在同一目录后双击 CMD。它优先调用 PowerShell 7，缺失时使用 Windows PowerShell，并使用
`-NoExit` 保留控制台；不绕过执行策略。即使 PS1 解析或策略检查失败，也能看到错误；
PowerShell 进程退出后 CMD 仍暂停。成功连接后也保留窗口，服务端窗口仍须单独保持打开。

本机需 OpenSSH、已核验的主机指纹和免密认证。默认 GUI 路径为
`D:\Software\ParaView 6.2.0\bin\paraview.exe`，客户端和服务端须同版本。
SSH 账号、入口和本机路径在 PS1 配置区维护；远端安装与执行节点见 [主入口](../SKILL.md#环境与执行方式)。

PS1 下载桥客户端，在编译节点启动 pvserver 并建立 SSH 隧道，再打开 GUI。
外部 SSH 入口已在编译节点时直接启动，其他入口则再跳转；本机连接为 `localhost:11115`。
启动编号、客户端源码和实际连接的回执匹配后显示 `Bridge ready`。
它不重建上次管线、不自动加载数据。端口被占用时先检查已有窗口和隧道，避免重复启动。

默认服务为 1 进程，可在新建连接时配置：

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
或使用客户端打印的完整会话目录替代 `current`。命令须串行；投递器用会话锁拒绝并发调用，
不自动排队或重发。超时不代表取消，先在
`/home/share/iosoeqkp/home/Liuzhaohui/.cache/paraview-bridge/` 对应 `session-UUID` 中
核对 `command.json` 与同 ID 的 `result.json`，不能盲目重发。

## 时间轴诊断

`inspect` 的 `timekeeper` 给出全局当前时间和可选时间值，`view_details.view_time` 给出各视图
当前时间，`source_details.time_properties` 给出每个源实际暴露的时间属性（包括 TimestepValues）。
该查询只读，不调整时间、读取器配置或管线。未连接时 timekeeper 为 null。

若全局同时出现 0/1/2/3 与 25/50/75/100，应分别核对各读取器及 PlotFile Header，不能直接把
下拉选项解释为统一帧索引。不同数据源可提供不同时间标度，时间轴合并后同一全局时间不保证
对应相同求解器步数；做 A/B 比较前先统一实际时间标度。

## FXAA 渲染警告

出现 `vtkOpenGLFXAAFilter` 无法取得 shader program 的警告时，先用 `inspect` 查看各视图
`fxaa.proxy`、`fxaa.client_view` 与 `fxaa.renderers`。这些是当前实际状态，不仅是代理属性。

```bash
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current disable_fxaa --timeout 180
```

可用 `--view-id '<ID>'` 限定一个视图，省略则处理当前全部 RenderView。操作关闭代理、客户端
视图及实际主/叠加 renderer 的 FXAA，逐个重绘并回传前后状态与本次客户端渲染的新消息；
临时消息收集器在成功或异常后均恢复。保留连接、数据、相机和显示对象，不清空历史错误。
新建视图仍可能使用默认开启设置；本操作不修改全局首选项，也不解决显卡驱动或 shader 编译根因。

## 应用已有 2DVorticity 排版脚本

`apply_2d_vorticity_trace` 在空数据管线中加载仓库 `后处理脚本/2DVorticity.py` 的原始排版。
仅替换读取器的单帧输入、注册名称，并按该帧实际时间同步动画和视图；保留原脚本的两个
布局、求导与切片、坐标标题、色标及相机。已有数据源时拒绝操作，避免覆盖用户管线。
脚本由打包器嵌入客户端，不接受外部 Python 或脚本路径；修改原脚本后须重新打包并热更新。

```bash
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current apply_2d_vorticity_trace \
  --input /absolute/shared/plt_128000 --time 1000 --timeout 600
```

回执包含实际输入、时间、出图视图 ID、切片单元数及标量范围。当前入口只加载指定单帧；`--time` 必须与输入 Header 的物理时间一致（单帧读取器可能不提供 TimestepValues）。

## 原位扩展 PlotFile 时间序列

先用 `inspect` 核对当前读取器的实际注册名称（名称可能因重命名或加载状态而改变），
不要假定仍叫 `plt_128000`。`set_plotfile_series` 接受同一目录下按步号严格递增的
速度 PlotFile 完整路径，原位更新读取器，保留下游管线、显示、相机和排版。

```bash
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current set_plotfile_series \
  --source 'plt_..' --files /absolute/run/plt_003200 /absolute/run/plt_006400 --timeout 300
```

操作核对读取器为每个文件提供递增物理时间，再同步动画范围、TimeKeeper 和视图时间到
最后一帧并渲染活动视图。失败时尝试恢复原文件列表与时间设置；断线不在恢复保证内。
调用前核对各输入 Header 和数据文件完整性，避免把缺失输出当作可用快照。

## 保存 GUI 场景供批处理复用

`snapshot_state` 只读保存当前场景，回传 `state_xml`、活动视图 ID 与尺寸，不改变 GUI
管线或相机。XML 通过客户端临时文件生成并回传，临时目录随后清理。投递端可将 XML
保存为共享目录的 `.pvsm`，由独立 `pvbatch` 加载；输入文件路径仍以场景记录为准。
这用于保留用户实际调整后的色标、边界线、相机和排版，避免从旧脚本近似重建。

```bash
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current snapshot_state --timeout 60
```

## 使用层级字段生成动态 AMR 框线

`add_level_array_outline`针对已核对为顶层lev0/1/2三组的切片，增加一个Programmable Filter。
默认输出类型保持`Same as Input`，每次RequestData按真实分组重新写入Field Data数组AMRLevel，
其Outline表示按该字段分类着色（白/红/绿），隐藏旧的自动块色框线；保留旧节点与涡量面。
Script内的`self`由过滤器执行环境提供，不能直接粘贴到Python Shell。当前6.2菜单中的显式
类型拼写为`vtkMultiblockDataSet`，与VTK类名`vtkMultiBlockDataSet`大小写不一致。

```bash
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current add_level_array_outline \
  --source Slice3 --view-id '<涡量视图ID>' --time 200 --timeout 600
```

可选`--time`执行当前时间→指定时间→当前时间往返检查：核对各非空块字段等于真实分组层号，
并核对显示仍使用AMRLevel、旧框线仍隐藏；结束恢复原时刻。可选`--output`保存原尺寸GUI截图。
操作拒绝默认覆盖同名节点；仅显式指定`--existing-source NAME`时修复对应的同输入Programmable Filter。
显示事务同时保存既有节点Script，异常恢复或回收新对象。更新后先检查输出非空，再Fetch和渲染，
避免VTK脚本错误未转成Python异常时把空输出误判为成功。脚本支持MultiBlock/MultiPiece等
Composite分组，不能仅递归MultiBlock。MPI `Fetch` 可能按进程再次包装输出，校验遍历所有
非空叶子的整数 AMRLevel 字段，不把聚合后的外层序号当成 AMR 层号。保存PVSM可保留Script和配色。

遇到 “Could not initialize annotations for categorical coloring” 时，先检查过滤器是否 Apply 成功、
输出是否非空以及 AMRLevel 是否为整数 Field Data；该提示本身不证明数据有超过32个层级。
操作会先更新有效输出，再设置 `0→lev0 / 1→lev1 / 2→lev2` 注释和颜色，最后启用分类着色。
空输出或旧脚本须先修复，单纯点击不再提示不能修复管线。

## 固定 AMR 框线的层级颜色

`color_outline_levels` 用于已核对为三层AMR分组的切片：顶层三个分组必须依次对应lev0/1/2。
它查找指定视图中已有的Outline表示，关闭其自动数组着色，按层级分组选择器显式指定
白/红/绿。`vtkBlockColors`是自动块色，不能当作AMR层号；分组实际含义须先按数据确认。
操作复用排版事务保存显示属性，异常时恢复；不改求导、切面或输入文件。
这是直接显示属性设置；实际GUI中曾在时间切换后被重置，动画优先使用上述字段方法。

```bash
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current color_outline_levels \
  --source Slice3 --view-id '<inspect返回的涡量视图ID>' --timeout 180
```

可用`--output /absolute/shared/proof.png`保存当前GUI视图供核对；截图保持原视图尺寸且不缩放字体。
成功回传包括旧数组/块颜色、实际层级选择器、新颜色及视图时间。用GUI保存状态或`snapshot_state`
可保留设置；此操作不更新原有动画PNG。

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

启动器现在保存独立服务窗口的 SSH 合并输出（带本地时间戳的 `.server.log`），包括认证、
跳板和远端日志初始化之前的错误；服务正常退出时保存 `.server-exit.json` 的退出码。
每次启动检查结束、启动异常及服务退出分别生成 `diagnostics_启动编号_*.zip`，各快照互不覆盖。
原生预检每次尝试的 stdout/stderr 与退出码另存 `.native.log`。

出现后续 GUI 崩溃或手动关闭服务窗口时，双击 `paraview_logs/collect_latest.cmd`，
将最新的 `diagnostics_启动编号_collected.zip` 发送即可；也可运行 PS1 的 `-CollectDiagnostics`。
此操作只收集最新启动已有的本地日志，不连接远端或关闭窗口。归档允许共享读取正在写入的日志，
并附带过去24小时中涉及ParaView的Windows应用崩溃/WER事件；无事件或权限不足时保留查询说明。
启动器和服务窗口被强制终止时，退出码与自动归档可能来不及生成，已落盘输出仍可重新打包。
GUI未写入stdout/stderr且未产生Windows崩溃事件的消息，仍不能保证全部捕获；本地服务日志
也无法替代SSH服务器内部的拒绝原因。旧版本须重新下载PS1后才具备这些功能。


- 服务端断线：启动器自动将服务 stdout/stderr 同时写入共享仓库
  `.tools/paraview-mpi/logs/launch-启动编号/pvserver_日期时间_随机后缀.log`，路径也显示在启动窗口中；
  关闭 Windows 窗口后日志仍保留。版本预检不生成日志，日志不自动清理。
  新版 PS1 在脚本旁 `paraview_logs/` 保存带时间、端口、唯一启动编号的 `.cmd` 与 `.json`，
  后者关联服务进程数、远端日志目录、客户端 stdout/stderr 和成功后的桥接回执 `.bridge.json`。
  同端口再次启动不会覆盖旧记录；旧 PS1 须重新下载，已有窗口无需为此重连。
  客户端 stdout/stderr 仅捕获实际写入这两个流的内容，不保证包含完整 Output Messages
  或调用栈；GUI 的 Output Messages 仍可用 `Save to File…` 单独保存。启动命令窗口仍需保持打开。
  `PARAVIEW_SERVER_LOG_DIR` 可覆盖远端日志目录。退出码 0 不代表此前没有协议错误。
  已观察到的 `Tag mismatch`（188968 为取消检查标签）仍未确定根因，不能把涡量修复或
  Linux 客户端层级切换通过当作 Windows GUI 断线已修复；需保留首次错误和触发操作。
- PS1 窗口立即关闭：新版会把启动异常写入脚本旁 `paraview_logs/launcher_error_*.log`，
  若该目录不可写则退回系统临时目录；交互式控制台会等待回车后再以失败退出。
  PS1 在首次 SSH 前开始 `launcher_时间_启动编号.log` 完整终端记录，结束时停止记录，
  不依赖较晚才生成的 pvserver 参数 JSON。文件解析及执行策略拒绝发生在脚本运行前，
  无法由 PS1 自身记录，使用上述 CMD 入口保留窗口。
  自动化可使用 `-NoPauseOnError`，重定向输入或非交互式控制台不会暂停。
  尚未更新脚本或需要查看解析/参数绑定错误时，在已打开的 PowerShell 中使用
  `pwsh -NoProfile -NoExit -File "本机实际路径/connect_paraview.ps1"` 保留完整报错。
  错误日志是定位依据，窗口保留不代表连接故障本身已解决。
- 版本预检得到空值：重新下载 PS1；当前实现读 EXE 版本资源，缺失时用同目录 pvpython，不能依赖 GUI `paraview.exe --version` 的管道输出。
- PythonQt 缺失：确认使用最新桥客户端，且在 ParaView GUI 内加载，而非普通 Python。
- 只有 timer/registered 输出：检查 GUI、SSH stderr 与启动回执；注册或新鲜心跳不代表 GUI 已响应。
  `Exceeded MaxStartups` 表示 SSH 入口在认证前拒绝新连接，不能据此判断密钥错误或认定
  连接积压来自本机。启动器对此明确错误最多重试五次，等待 4、8、16、32、60 秒。
  节点名、版本、客户端清单、端口/回执查询及桥脚本下载等只读操作，也对连接关闭、重置、
  超时按同样上限重试。认证/主机指纹错误不重试；GUI动作投递等有副作用的操作
  不因普通断线重放。服务SSH仅在每次独立的verbose日志确认握手已开始、尚未认证、
  未发送命令且没有认证/指纹错误时，对连接关闭、重置或MaxStartups最多重试五次；
  认证成功后的断线和139等服务崩溃不自动重启。缺少握手证据时也不重试。SSH传输失败与版本检查不匹配分别报错，空版本输出不当作版本不兼容。
  正常启动将节点名、版本、清单、桥脚本字节和整个候选端口范围合并到一次只读SSH预检，
  不再额外scp或逐端口连接；仍本地核对文件哈希，服务监听从本次本地服务输出检查，GUI回执仍独立检查。
  无重试且仅一个窗口时，Windows到入口的连接为4次（预检、服务隧道、桥接长连接、GUI回执）。
  服务握手期间不再并发发起监听查询SSH；本地等待覆盖有界重试，服务退出会直接报告退出码。
  每次握手单独保存`.ssh-attempt-N.log`，不把认证后断线当作可安全重试。
  版本 28 桥接在一个 SSH 长连接上收发请求，不再每轮新建连接；传输失败后按
  4、8、16、32、60 秒退避，恢复成功后重置。认证错误停止桥接；GUI 命令仍按 ID 去重，
  断线不自动重放 GUI 操作。注册沿用最多三次的有限重试。
  传输协议已升级，旧窗口不能通过热更新安装这一传输改动；当前窗口可继续使用，
  下次正常启动前重新下载 `connect_paraview.ps1`，它会自动同步新版单文件客户端。
  持续限流仍需管理员检查入口 SSH 服务，不保证客户端重试可以解除服务端限制。
- 超时、源码哈希或 URI 不匹配：保留窗口与诊断文件，先定位连接、客户端或传输层；不自动关闭用户窗口。
- SSH 指纹或认证失败：核验目标和认证，不关闭主机验证来绕过。

当前操作是否成功以对应命令回传为准；远端测试不能代替 Windows 本机的完整启动验收。

## 仅显示整体立方体外框

`domain_outline` 使用已加载三维 AMReX 读取器的全域 bounds 创建独立 Box，
以黑色 Wireframe 显示，隐藏目标视图内可见的 Outline 表示，保留切片、时间、相机和旧节点。
采用单个几何盒避免 MPI 分块和 AMR 各块分别生成框线；动态切换时间仍保持此固定计算域。
仅适用于计算域不随时间改变的场景。已有同名外框时拒绝重复创建；显式给出 `--existing-source Domain_outline_视图ID` 可复用该 Box。

```bash
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current domain_outline \
  --source 'plt_..' --view-id '<目标视图ID>' --timeout 180
```

操作复用排版事务，异常时恢复显示并回收新对象。

## 试用 Viridis 色标

`viridis --source Publication_vorticity_magnitude --view-id <ID>` 仅修改现有可见标量的颜色传递函数，
保留数值范围、透明度、时间、框线和布局；共享该色表的视图一起刷新。
优先尝试 Viridis (matplotlib)，缺少时使用当前版本的 Viridis；失败复用排版事务回滚。
