# 超算独立 ParaView 批处理

本入口无需本机 ParaView、Windows 启动脚本、SSH 隧道或控制桥在线。
`pvbatch` 在独立进程中读取共享目录的 PlotFile，普通 CSV 分析和绘图仍使用根 `.venv/bin/python`。

## 节点与运行环境

**ParaView 固定在编译节点 `whshare-agent-1` 执行。**
[运行入口](../scripts/run_paraview_batch.sh) 和 [安装入口](../scripts/install_paraview_batch.sh) 若在其他节点调用，
先通过 SSH 跳转到该节点；跳转失败即退出，不回退到登录节点执行。
共享路径使登录节点只需发起命令、收集结果，不承载长期 ParaView 进程。

系统模块 ParaView 5.11.1 的 `paraview-config.cmake` 明确为 `PARAVIEW_USE_PYTHON=OFF`，
无法直接提供这条路线。独立安装使用 conda-forge 的 Linux aarch64 ParaView **6.2.0**，
应用前缀为仓库 `.tools/paraview-mpi/env`，同时提供MPI服务端与独立批处理，不替换系统模块或shell启动配置。
应用自带 Python 3.12 供 pvbatch 使用，不是额外的通用后处理 Python 环境；
统计和绘图不在此环境运行。依赖由[显式锁文件](../scripts/paraview-mpi-linux-aarch64.lock)
固定88个带SHA256的包（Python、MPICH、编译器、EGL等），使用TUNA conda-forge镜像。
ParaView从官方6.2.0源码构建，SHA256固定为
`373afd566ef5f6c650c3acf4a5d4f43ad75975537f7fe225cb53ed20fe41cd20`，开启MPI/Python/EGL，关闭Qt界面。
源码与目标小文件在编译节点本地临时目录构建，共享前缀只保留安装、源码归档、依赖缓存与日志。
可用PARAVIEW_BUILD_JOBS设置构建并发数（默认32）、PARAVIEW_BUILD_ROOT指定本地构建目录。
安装器对同一前缀加锁；已安装匹配的依赖与MPI应用时不重复构建。
micromamba 2.9.0的SHA256保持上游固定校验；不依赖系统MPI环境。

从仓库根安装或核对环境：

```bash
bash .agents/skills/paraview/scripts/install_paraview_batch.sh
```

依赖来源：[conda-forge ParaView](https://anaconda.org/conda-forge/paraview)，
[ParaView Python 文档](https://www.paraview.org/paraview-docs/latest/python/)。

## 独立导出中心线

先在共享目录创建自己的输出目录，然后从仓库根运行：

```bash
bash .agents/skills/paraview/scripts/run_paraview_batch.sh \
  --input /absolute/path/to/plt_256000 \
  --output /absolute/existing-directory/centerline.csv \
  --metadata /absolute/existing-directory/paraview_export.json \
  --resolution 1024 --max-level 2
```

输入/输出必须为共享存储中的绝对路径。元数据记录真实执行主机、pvbatch 路径、
ParaView 版本、耗时、几何范围、采样端点与点数。
运行入口会清除继承的 Python、动态库和显示相关配置，以免误用登录环境中的其他版本。

[批处理入口](../scripts/paraview_batch_export.py) 以“只加载定义”的方式复用
[桥客户端](../scripts/paraview_bridge_client.py) 中的同一个 `export_centerline` 函数：
AMReX reader → ExtractBlock /Root → CellDataToPointData → PlotOverLine。
此模式不会启动 GUI 定时器、SSH 轮询或控制桥注册，也不控制用户当前窗口。
CSV 用 17 位精度导出；样本有效性和数值比较由上层分析脚本检查。
可以串行对多个 PlotFile 调用此命令，每次指定不同输出文件，不受桥接单槽协议限制。

## BOX3D_OSI Re=1000 已验证流程

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/data_post_processing/extract_run_centerline.py \
  projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261015_171801_job613495 \
  --backend batch
```

默认 `--backend bridge` 仍保留；batch 输出使用独立的 `_paraview_batch` 目录，不覆盖桥接结果。
加 `--reuse-export` 可只用已有 CSV 重绘和分析，不要求任何 ParaView 进程在线。

2026-10-19 实测：job613495 最新 plt_256000，Re=1000，AMR level 0–2，
中心线 (0.5,0.5,0) → (0.5,0.5,1)，1024 段得到 1025 个有效点。
元数据确认执行主机为 whshare-agent-1，数据管线与导出约 14.78 秒。
与先前 ParaView 5.11 系列桥接结果逐点比较，坐标及三分量速度最大绝对差为 0。
CSV 字节哈希不同，因为两个版本的输出列顺序不同；比较按列名与坐标进行。
此验证针对同一 PlotFile 的中心线提取，不替代全三维数值正确性或收敛证明。
独立 pvbatch 路线验证覆盖数据提取和 Matplotlib 对比绘图；其截图/动画仍未验证。
6.2.0 client/server 的单张离屏截图证据见 [桥接验证边界](bridge.md#故障处理与验证边界)。
