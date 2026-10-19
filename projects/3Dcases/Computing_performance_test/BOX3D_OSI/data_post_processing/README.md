# 参数化后处理入口

从仓库根执行，统一使用根 `.venv/bin/python`。先设置算例相对路径：

```bash
case_dir=projects/3Dcases/Computing_performance_test/BOX3D_OSI
post_dir="$case_dir/data_post_processing"
```

| 入口 | 职责与输出 |
|---|---|
| [plot_centerline.py](plot_centerline.py) | 一条/多条中心线与可选参考曲线；图片、无量纲点列 `.csv`、误差 `.errors.csv`、来源与指标 `.json` |
| [plot_convergence.py](plot_convergence.py) | 日志/CSV/受限 PlotFile 收敛趋势；图片与统一六列 CSV |
| [plot_run_performance.py](plot_run_performance.py) | 已有性能日志概览；每窗步数通过 `--window-steps` 指定 |
| [extract_run_centerline.py](extract_run_centerline.py) | 保留 ParaView bridge/batch 导出、来源校验、CSV 复用和报告，调用共用中心线绘图函数 |
| [convergence_logs.py](convergence_logs.py) | 内部日志解析模块，供收敛入口调用 |

作业号、步数、Re 标题、路径和图例改参数，不再为每次实验复制脚本。
三个绘图入口使用明确的 `--output`；以下示例使用独立 `results/` 路径。
重复使用同一输出路径会覆盖已有文件，运行时应选新名称。

## 中心线：多步叠加

```bash
.venv/bin/python "$post_dir/plot_centerline.py" \
  --profile "$post_dir/64000_3D.csv" t64k 64000 \
  --profile "$post_dir/96000_3D.csv" t96k 96000 \
  --profile "$post_dir/160000_3D.csv" t160k 160000 \
  --height 128 --lid-velocity 0.05 --origin 0 --round-z 6 --allow-unmasked \
  --title 'Re=1000 centerline evolution' --xlim -1 1 \
  --output "$post_dir/results/Re1000_evolution.png"
```

`--profile CSV LABEL STEP` 可重复，标签须唯一；每条曲线记录输入文件和步数。
`--height`、`--lid-velocity` 必须明确指定，归一化为 `(z-origin)/H` 和 `ux/U_lid`。
坐标列自动识别 `Points:2`、`Points_2`、`Points_Z`，也可指定 `--z-column` 和 `--velocity-column`。
`--round-z` 控制聚合精度，默认不舍入；旧 Re1000 曲线使用6位，旧 Re3200曲线使用9位。

默认要求 `vtkValidPointMask`；有掩码时跳过无效端点，拒绝跨越内部无效采样，拒绝非有限值。
没有掩码的旧 CSV 只能显式加 `--allow-unmasked`，报告保留该选择；这不构成采样有效性证明。

## 中心线：与参考数据对比

```bash
.venv/bin/python "$post_dir/plot_centerline.py" \
  --profile "$post_dir/Re3200_288000.csv" Re3200 288000 \
  --height 128 --lid-velocity 0.05 --round-z 9 --allow-unmasked \
  --reference "$post_dir/fig18b_velocity_profiles.csv" \
  --reference-source 'Jaber Fig.18(b), existing digitized CSV' \
  --title 'Re=3200, step 288000 vs Fig.18(b)' --xlim -1 1 \
  --output "$post_dir/results/Re3200_reference.png"
```

参考 CSV 必须已经无量纲化，列为 `z`/`z_over_H`、`u_over_ulid`，可含多组 `series`；
参考数据不再按仿真 H/U 重复缩放。误差在参考点处线性插值仿真值，输出 MAE、RMSE、
最大绝对误差、使用/排除点数。默认超出仿真范围时报错；显式加
`--outside-reference exclude` 才排除范围外点并计数，不做外推。
所有曲线共用该次调用的归一化和列选择；不同尺度的数据分次调用，或先准备统一的 CSV。
参考吻合不能据此判定收敛。

## 收敛：日志、已有 CSV 与 PlotFile

```bash
# 多份续跑日志，后传入文件覆盖同一步；阈值默认取最后正式记录。
.venv/bin/python "$post_dir/plot_convergence.py" \
  --log <第一段日志> --log <续跑日志> --restart-step 96000 \
  --layout relative --output "$post_dir/results/convergence_logs.png"

# 当前保留的历史收敛 CSV，不要求恢复已缺失的完整提交日志。
.venv/bin/python "$post_dir/plot_convergence.py" \
  --csv "$post_dir/596154_Re3200_convergence.csv" --tolerance 1e-4 \
  --restart-step 96000 --restart-step 192000 --layout relative-and-norm \
  --title 'Re=3200 historical convergence records' \
  --output "$post_dir/results/convergence_csv.png"

# 从保存的 Level-0 速度重建差值，仍只接受原入口的专用二进制格式。
.venv/bin/python "$post_dir/plot_convergence.py" \
  --plotfile-run <运行目录> --plotfile-interval 3200 --tolerance 1e-4 \
  --output "$post_dir/results/convergence_plotfile.png"
```

三种来源互斥。`--layout relative` 只画相对变化，`relative-and-norm` 同时画速度 L2 范数；
`--restart-step` 可重复；可用 `--csv-output` 单独指定导出位置，默认跟随图片 basename。
导出统一为 `record,step,interval,velocity_l2_relative,velocity_l2,delta_velocity_l2`。
已有不含 `record` 的五列 CSV 按趋势读取，有该列时保留 trend/formal 区别。

CSV 和 PlotFile 必须显式传 `--tolerance`：原 CSV 未保存 tolerance、consecutive、converged，
不能恢复正式收敛状态。只有日志输入报告原始最后一次正式 `converged` 标志，CSV/PlotFile 输出为 null；
低于绘图阈值不等于连续次数满足或程序已收敛。PlotFile 读取仍限于单块、无 ghost、
Level-0、128³、三分量双精度的现有布局，并核对数据与 Cell_H 极值；不能用于任意 AMReX 布局。

## 历史入口替换与复现边界

| 旧脚本 | 新调用中的关键参数 |
|---|---|
| `plot_Re1000_3D_overlap.py` | 中心线多次 `--profile`，H=128、U=0.05、`--round-z 6`、显式无掩码模式 |
| `plot_overlap_160000_fig18a.py` | step160000、同上尺度和6位精度，`--reference fig18a_velocity_profiles.csv` |
| `plot_Re3200_3D_overlap_288000_fig18b.py` | step288000、H=128、U=0.05、9位精度，`--reference fig18b_velocity_profiles.csv` |
| `plot_586660_convergence.py` | `--log` 两段日志、`--restart-step 96000 --layout relative` |
| `plot_596154_convergence.py` | `--log` 三段日志、重启点96000/192000、`--layout relative-and-norm`，或使用已保存六列CSV |
| `plot_plotfile_convergence.py` | `--plotfile-run`、间隔3200、显式阈值；保留原格式限制 |

586660/586864 和 596154/596532 的默认提交日志当前未保留，596888 最后一段日志仍存在；
本次不宣称完整历史日志重新验收。已有 CSV、图像和报告全部保留；仿真曲线来源不完整时仍须标注。
两个专用数据准备/参考比较脚本及硬编码饼图见[归档说明](archive/README.md)。
ParaView 的导出协议、校验和报告保留；提取入口按以下方式调用：

```bash
.venv/bin/python "$post_dir/extract_run_centerline.py" <运行目录> \
  --backend batch --resolution 1024 --reuse-export
```

它选择该运行目录中有 Header 的最大步数 PlotFile，仅支持 Re=1000/3200，U_lid固定0.05；
输出固定为本目录的 `<run-name>_step<step>_paraview[_batch]/`，不接受 `--output`。
`--reuse-export` 校验并复用该目录已有 CSV/元数据，仍会重写图片、误差及报告；
省略它会调用对应 ParaView 后端重新导出。此入口的掩码检查仍默认严格，
范围外参考点继续按原逻辑排除并记录，不套用普通 CSV 的无掩码选项。
