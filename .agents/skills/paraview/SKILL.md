---
name: paraview
description: 在本仓库使用 ParaView 处理 AMReX 三维数据，包括通过 Windows GUI 与远端 pvserver 的控制桥操作当前窗口，以及在超算编译节点独立运行 pvbatch 后台导出。用户提及 ParaView 桥接、连接脚本、自动加载、远端后台后处理、批量提取中心线或桥接故障时使用；普通 CSV 绘图、求解器修改不触发。
---

# ParaView 后处理

## 选择执行方式

- 需要独立自动处理共享数据、不依赖 Windows 在线：使用远端批处理，先读 [batch.md](references/batch.md)。
- 需要操作用户当前 GUI 的管线、让提取结果保留在窗口：使用控制桥，先读 [bridge.md](references/bridge.md)。
- 用户指定方式时按指定方式执行。批处理创建独立会话；超算端不能直接启动用户 Windows 上的 PS1。

这是本仓库的项目 skill。命令均从仓库根执行，核心脚本唯一实现位于 `scripts/`。旧后处理目录仅保留两个 Windows 连接入口的兼容软链，不维护第二套实现。

## 共同执行步骤

1. 核对输入 PlotFile 的绝对路径及 Header，从运行快照确认 Re、步数、变量、AMR 层级和归一化参数。“最新”按数值步号选择，不按修改时间猜测。不把 job613495 的 Re=1000 当作 Re=3200。
2. 在共享存储创建专属输出目录，确认采样位置、分辨率和读取层级。默认实现仅提供几何中心沿 z 方向的速度中心线，不能冒充任意截面、流线或全场导出。
3. 运行选定入口。批处理及安装探测必须在 **whshare-agent-1 编译节点**执行，使用提供的包装脚本自动 SSH；失败即停止，绝不在登录节点直接调用 pvbatch。GUI 脚本经配置的 SSH 入口转到同一编译节点；服务包装脚本检查主机名，拒绝在其他节点启动。当前 6.2.0 构建无 MPI，GUI 服务使用单进程。
4. 检查对应成功回传或批处理元数据，记录主机、版本、输入、步数、端点和读取层级。批量任务为各输入指定独立输出。桥接单槽命令必须串行；超时后先核对原命令结果，不能盲目重发。
5. 用仓库 `.venv/bin/python` 分析 CSV，核对 vtkValidPointMask、有限值、点数及坐标。跨版本按列名和坐标比较，不只比较哈希。图表标注输入、步数、归一化和参考来源。中心线提取成功不构成全三维正确性或收敛证明。

## 最小入口

桥接已由本机启动脚本建立后，探测实际连接与能力：

```bash
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current inspect
```

独立批处理先核对/安装锁定环境，再执行导出（输出目录须存在）：

```bash
bash .agents/skills/paraview/scripts/install_paraview_batch.sh
bash .agents/skills/paraview/scripts/run_paraview_batch.sh \
  --input /absolute/shared/plt_256000 \
  --output /absolute/shared/output/centerline.csv \
  --metadata /absolute/shared/output/paraview_export.json \
  --resolution 1024 --max-level 2
```

ParaView 使用应用自带 Python；其他分析只用仓库 `.venv`。应用保留在 `.tools/paraview-batch/env`，不迁移二进制到 skill，不修改系统模块或 shell 启动文件。

## 实现与维护

- `connect_paraview.ps1`：Windows 6.2.0 客户端与远端同版本预检、下载桥客户端、启动连接并校验本次 GUI 回执。
- `run_paraview_server.sh`：限定编译节点、隔离旧模块环境，启动无 MPI 的单进程 6.2.0 pvserver。
- `paraview_bridge_client.py`：GUI 主线程执行白名单命令，后台线程只处理 SSH；builtins 持有唯一桥实例及原生回调。已有窗口不会随仓库源码自动更新。
- `paraview_bridge_send.py`：通过共享注册和心跳发现会话，单槽投递并等待对应 ID 回传。
- `paraview_batch_export.py`：只加载桥模块定义，复用 export_centerline，不启动定时器或桥注册。
- `run_paraview_batch.sh`、`install_paraview_batch.sh` 和显式包锁：节点路由、应用环境隔离和安装。安装状态来自 conda-meta，不另设缓存标记。

修改中心线算法时维护两种入口共用的函数。新增操作先核对现有能力和 API，再实现、测试并更新能力声明；不能通过文件协议投递任意 Python。算例物理参数、参考数据及绘图保留在算例的 data_post_processing 中。

迁移或修改后至少执行：

```bash
bash -n .agents/skills/paraview/scripts/run_paraview_batch.sh
bash -n .agents/skills/paraview/scripts/install_paraview_batch.sh
.venv/bin/python -m unittest discover -s .agents/skills/paraview/scripts/tests -v
```

模拟测试覆盖启动回执、生命周期和节点路由，不能代替 Windows 现场启动。数据路径变化还需在编译节点对真实 PlotFile 导出并核对 CSV。截图、动画及并行 pvbatch 尚未纳入已验证能力。
