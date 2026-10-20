# 远端独立批处理

运行环境见 [主入口](../SKILL.md#环境与执行方式)。批处理创建独立会话，不改变用户 GUI。

## 现有中心线导出

[包装脚本](../scripts/run_paraview_batch.sh) 自动 SSH 到编译节点，失败即退出，
并清除继承的 Python、动态库和显示配置。它固定执行中心线导出，不接受任意 Python 脚本。

在共享存储创建输出目录后，从仓库根调用：

```bash
bash .agents/skills/paraview/scripts/run_paraview_batch.sh \
  --input /absolute/shared/plotfile \
  --output /absolute/shared/output/centerline.csv \
  --metadata /absolute/shared/output/paraview_export.json \
  --resolution 1024 --max-level 2
```

输入、输出均使用共享存储绝对路径。该命令沿三维 bounds 的几何中心从 z_min 到 z_max
采样 `ux/uy/uz`；1024 段产生 1025 点，`--max-level` 是请求的最大读取层级。
CSV 使用 17 位精度；JSON 记录实际主机、版本、耗时、几何范围和采样端点。
批量输入逐个指定独立输出，导出后检查实际层级、有效掩码、有限值及坐标。

算例已有提取入口时优先复用；例如 BOX3D_OSI 的后端选择与分析用法见其
[后处理说明](../../../../projects/3Dcases/Computing_performance_test/BOX3D_OSI/data_post_processing/README.md)。

## 运行其他 ParaView Python 管线

截面、流线、截图等任务使用应用自带 `pvbatch` 执行所需的 ParaView Python 脚本。
先查所属算例与公共后处理工具；没有合适入口时再编写任务脚本。输入和产物放共享存储，
一次性脚本按仓库规则归入所属范围的 `scripts/temporary/`。

下例中的脚本路径须替换为实际文件；通过 SSH 在编译节点执行，SSH 失败即停止。
若当前已在该节点，直接执行 heredoc 内的命令，无需 SSH 回连自身：

```bash
env -u LD_LIBRARY_PATH ssh -T -o BatchMode=yes -o ConnectTimeout=10 whshare-agent-1 bash -s <<'REMOTE'
set -euo pipefail
[[ "$(hostname -s)" == whshare-agent-1 ]]
cd /home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex
env -u LD_LIBRARY_PATH -u PYTHONPATH -u PYTHONHOME -u DISPLAY \
  -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH -u PV_PLUGIN_PATH \
  QT_QPA_PLATFORM=offscreen \
  .tools/paraview-mpi/env/bin/pvbatch --force-offscreen-rendering \
  /absolute/shared/task.py
REMOTE
```

脚本内使用 `paraview.simple` API，按数据格式选择读取器；AMReX 输入先确认读取器的
实际属性、数组关联与 composite 层次，再接过滤器。ParaView Python API 可查
[官方文档](https://www.paraview.org/paraview-docs/latest/python/)，以当前安装支持的属性为准。
截图或动画需在脚本中设置视图、相机、色标与输出，不能把中心线导出当作渲染入口。

默认单 rank。确需多 rank 时使用同一安装的 `bin/mpiexec -n N bin/pvbatch`，
并验证过滤器输出；现有中心线在串行与 4 rank 下曾出现速度差异，不能默认数值等价，
证据见 [MPI 安装与验收记录](../../../../docs/ai_changes/2026-10-20-paraview-mpi-install.md)。

## 环境缺失或损坏

找不到 `pvbatch` 时读 [安装与维护](maintenance.md#安装环境)。普通使用不必每次运行安装器。
执行失败先检查 SSH、输入路径、应用路径与过滤器报错，不回退到登录节点或系统 Python。
