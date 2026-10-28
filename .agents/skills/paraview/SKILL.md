---
name: paraview
description: 在本仓库远端环境使用 ParaView：运行 pvbatch 后处理，或通过 Windows GUI 控制桥操作已连接窗口。用于远端执行、连接与排错；普通 CSV 绘图不触发。
---

# 远端 ParaView

## 环境与执行方式

ParaView 在编译节点 **whshare-agent-1** 运行，不在登录节点执行。仓库私有安装为
`.tools/paraview-mpi/env`，版本 **6.2.0**，支持 MPI/Python/EGL；使用该安装自带的
Python 和 MPI，CSV 分析与绘图仍用仓库 `.venv/bin/python`。以下命令从仓库根执行。

- **独立处理共享数据**：用 `pvbatch`，不要求 Windows 在线。先读 [远端批处理](references/batch.md)，包含现有中心线入口和其他 Python 管线的执行方法。
- **操作用户当前窗口**：用 GUI 控制桥。先读 [GUI 桥接](references/bridge.md)，包含本机启动、状态查询、可用命令与排错；涡量切片出图排版见 [涡量切片出图](references/publication.md)。远端不能直接启动用户 Windows 上的 PS1。
- 用户指定方式时按指定方式执行；未指定且无需保留 GUI 管线时用批处理。

## 最小入口

已有 GUI 桥时，先查询当前连接、数据源与能力：

```bash
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current inspect
```

独立导出中心线（输入为共享存储绝对路径，输出父目录须存在）：

```bash
bash .agents/skills/paraview/scripts/run_paraview_batch.sh \
  --input /absolute/shared/plotfile \
  --output /absolute/shared/output/centerline.csv \
  --metadata /absolute/shared/output/paraview_export.json \
  --resolution 1024 --max-level 2
```

## 执行时确认

核对输入 Header、步数、变量和实际 AMR 层级；“最新”按数值步号选择。按任务确定采样
位置、读取层级及输出路径，检查回传或元数据中的实际主机、版本和输入。中心线入口固定
沿几何中心的 z 方向采样，其他截面、流线或全场任务按需构建管线。

桥命令串行投递，超时不代表取消，先检查对应命令结果再决定下一步；缺少能力时先查
`client_status`，不要借文件协议执行任意 Python。导出后核对坐标、点数、有效掩码和有限值；
跨版本或进程数比较按列名与坐标进行，提取成功不能作为求解器正确性或收敛证明。

仅修改工具、安装环境或处理旧客户端时读 [实现与维护](references/maintenance.md)。
算例参数、参考数据和绘图方法以所属算例的 `data_post_processing/` 为准。
