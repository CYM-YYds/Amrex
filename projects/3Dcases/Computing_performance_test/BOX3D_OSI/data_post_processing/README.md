# 参数化后处理入口

本目录集中保存日常提取与绘图入口；构建/提交见 [scripts 索引](../scripts/README.md)，完整位置导航见[仓库脚本导航](../../../../../docs/script-guide.md)。
`convergence_logs.py` 是供 `plot_convergence.py` 导入的内部模块，不是单独绘图命令。

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
| [extract_run_centerline.py](extract_run_centerline.py) | 只负责 ParaView bridge/batch 导出、来源校验及提取报告；不绘图、不选择参考数据 |
| [pack_animation_gif.py](pack_animation_gif.py) | PNG序列选取帧段、统一裁剪、合成GIF并记录来源哈希 |
| [plot_osi_principle.py](plot_osi_principle.py) | 中文 D2Q9 OSI 原理简化动画：7×7 无 ghost，跟踪节点每步“读取→碰撞→写回→边界处理”，读取位置回绕、7 步一循环，边界层错误分量标红后被覆盖；GIF 与逐帧 PNG |
| [convergence_logs.py](convergence_logs.py) | 内部日志解析模块，供收敛入口调用 |

作业号、步数、Re 标题、路径和图例改参数，不再为每次实验复制脚本。
三个绘图入口使用明确的 `--output`；以下示例使用独立 `results/` 路径。
重复使用同一输出路径会覆盖已有文件，运行时应选新名称。

## OSI 原理动画

从仓库根执行，输出目录必须尚不存在：

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/data_post_processing/plot_osi_principle.py \
  --node 6 4 --output projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/osi_principle_animation/boundary_6_4
```

按用户参考图做的简化教学动画。7×7 存储环无 ghost 层：粗框内 5×5 为内部节点，框外一圈为边界层
（物理边界或通信边界）。只跟踪一个节点，每个时间步依次画“读取 → 碰撞 → 写回 → 边界处理”。
读取位置为 `(x − p·e_q) mod 7`，p 每步加 1：读取位置逐步移动，越界后从对侧回绕，
t+7 与 t 相同，每 7 步循环一次。

相位加一后，边界层节点“来自域外”的分量（上游 `x − e_q` 在 7×7 之外）读到的是对侧边界节点
写入的错误值，标红；下一帧由边界处理覆盖、恢复蓝色。存储地址回绕本身不代表值错误：内部节点
的分量即使地址回绕，读到的也是真实邻居写入的值。绘图前全域模拟核对：每步错误分量恰为 80 个，
全部是最外层节点来自域外的方向（边上节点 3 个、角点 5 个）。

`--node X Y` 选跟踪节点（0..6，默认右边界 6 4）；角点（如 6 6）有 5 个红色分量；内部节点
（1..5）没有红色分量，边界处理阶段只画一帧说明。默认 (6,4) 共 36 帧、880×1012、每帧 2400 ms，
一轮 86.4 秒；`--duration` 调帧时长，`--font` 指定中文字体。输出 `osi_principle.gif`、逐帧 PNG
（`frames/`；`source_frames/` 是合成器读取的同像素来源）和 `manifest.json`（逐帧位置、红色分量与命令）。

方向编号沿用参考图（1 右、2 上、3 左、4 下、5–8 为对角），与 `LbmLattice.H` 内部排列不同，
只作标签。绘图前核对图中位置与 `OsiIndex.H::osi_coord_precomputed` 一致；这是图示检查，
不读取流场数据，也不构成求解器数值验证。生产代码保留两层 ghost，并在相位提交前同步 ghost、
提交后重建物理边界（见 `docs/osi_algorithm_and_architecture.md` 第 4 节）；本图把这两类覆盖合并为
最外层的“边界处理”示意。

## job622356/B 涡量动画

现役序列为 [images/gif-B/job622356_job622356__B__victory.gif](images/gif-B/job622356_job622356__B__victory.gif)
与 [gif-B目录](images/gif-B/) 内同前缀 `.0000.png` 至 `.0039.png` 共40张图。GIF 每帧500 ms、无限循环，完整一轮20秒；
它是调色板预览，PNG 保留为逐帧图像。

输入为 `runs/a5_B_three_plot3200/job622356/B/plt_003200` 至 `plt_128000` 的三维速度，
步长3200、物理时间25至1000（间隔25）。沿用 GUI 场景先求三维旋度，再取 y=0.5 切片，
显示 `mag(Vorticity)*1.0`，不额外归一化、不平滑、无外部参考数据；色标0–1，超出部分饱和。
AMR lev0/1/2 以整数 Field Data `AMRLevel` 分类为白/红/绿，不能按自动 `vtkBlockColors` 推断层级。

原尺寸2110×1066渲染保持GUI字号；随后所有帧统一裁剪 `(616,0,1493,1066)`，最终877×1066，
无缩放或插值。正式输入/状态、驱动、逐任务日志、40帧回执、旧图和未裁剪图保存在
[`animation_reexport_amrlevel/`](../runs/a5_B_three_plot3200/job622356/B/figures/animation_reexport_amrlevel/)，
具体依据见[重导出记录](../docs/ai_changes/2026-11-03-vorticity-40frames-amrlevel.md)。这些是复现材料，保留。

`B/figures/vorticity_y05_step128000.*` 是早期读取求解器原生涡量模长的单帧结果，
不代表上述速度求导序列。其他早期补帧或原尺寸导出目录也不作为当前图像入口。
批处理复用当前GUI场景的方法见[时间序列出图](../../../../../.agents/skills/paraview/references/publication.md#从-gui-场景导出时间序列)。

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

## 复用现成CSV快速重绘

来源、有效掩码和归一化已核对且数据未变化时，只运行绘图入口；不重复提取或连接ParaView，
也不必重新全文检索同一篇论文。仅改变样式、标签或添加现有参考时不新增脚本。
若更换输入文件、截线、Re或标度，再核对相应来源。重绘只需检查新图和旁边的JSON/误差表，
无需重复完整数值验收；另取输出名以保留原图。

以下复用已保存的128³/256³在t=1000的中心线，叠加Re=1000的Fig.18(a)参考：

```bash
analysis_dir="$case_dir/runs/uniform_256_lev0_t1000/analysis"
.venv/bin/python "$post_dir/plot_centerline.py" \
  --profile "$analysis_dir/n128_t1000/centerline.csv" '128^3, t=1000' 128000 \
  --profile "$analysis_dir/n256_t1000/centerline.csv" '256^3, t=1000' 256000 \
  --height 1 --lid-velocity 0.05 \
  --reference "$post_dir/fig18a_velocity_profiles.csv" \
  --reference-source 'Jaber et al., Fig.18(a), existing digitized AMR and Cortes-Miller points' \
  --title 'Re=1000, t=1000: centerline vs Fig.18(a)' \
  --output "$analysis_dir/centerline_t1000_with_reference_redraw.png"
```

现成原图为`centerline_t1000_with_reference.png`；参考CSV包含20个论文AMR点和44个
Cortes-Miller点，已经无量纲化，不再除以U_lid。论文数字化参考不是原始真解表；误差按两组
分别报告，不挑选更有利的一组作为唯一精度结论。完整来源及现成结果见
[正式运行记录](../docs/ai_changes/2026-10-30-uniform-256-t1000.md)。

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
## 提取一次，独立绘图

提取与绘图是两个独立入口。已存在 `centerline.csv` 时直接绘图，无需重新提取或连接 ParaView。

```bash
# 第一步：只提取和校验，运行目录请替换成实际路径。
.venv/bin/python "$post_dir/extract_run_centerline.py" <运行目录> \
  --backend batch --resolution 1024 --output-dir /absolute/path/my_export

# 第二步：按需要修改CSV来源、参考数据、坐标范围和样式。
.venv/bin/python "$post_dir/plot_centerline.py" \
  --profile /absolute/path/my_export/centerline.csv MyRun 256000 \
  --height 1 --lid-velocity 0.05 --origin 0 \
  --reference "$post_dir/fig18b_velocity_profiles.csv" \
  --reference-source 'Jaber Fig.18(b), existing digitized CSV' \
  --xlim -0.4 1.05 --ylim 0 1 \
  --xlabel 'ux / U_lid' --ylabel 'z / H' \
  --figsize 9.4 7.5 --linewidth 1.8 --markersize 6.5 --dpi 220 \
  --title 'My centerline comparison' --output /absolute/path/my_plot.png
```

绘图示例的step、H、U和原点须与实际提取报告一致，参考数据按实验选择；省略 `--reference`
只画仿真。`--profile` 可重复，更换CSV或参考来源不需要改提取代码。
图的横轴默认是速度 `ux/U_lid`，纵轴是高度 `(z-origin)/H`；改标签不会改变数据定义。
`--xlim/--ylim` 只改变显示范围，不裁剪导出数据或改变参考误差计算范围。
可在 `plot_centerline.py` 的 `main()` 中编辑各参数的 `default=` 改长期默认样式，
或在 `plot_profiles()` 中编辑颜色、图例等；数据路径通过 `--profile/--reference` 传入。
样式参数与来源、误差一同记录在图片旁的JSON中。
默认采用论文黑白样式：6.4×5.8 英寸图幅、衬线字体、无网格、四边朝内刻度、右下角图例，横轴范围为 −1 到 1；仿真用细线，参考数据用空心标记。
图例使用传入的 LABEL，页脚保留步数与归一化参数，完整输入路径和参考出处保存在同名 JSON 中。

提取入口选择运行目录中有Header的最大步数PlotFile，读取运行快照的Re与几何；
不再限制Re=1000/3200，不再自动加载Fig18参考或160000步历史曲线。
默认输出目录仍为 `<run-name>_step<step>_paraview[_batch]/`，可用 `--output-dir` 指定。
提取输出为 `centerline.csv`、`paraview_export.json`、`extraction_report.json`，
新导出还写 `export.log`。报告包含来源哈希、步数、掩码检查、采样范围与归一化，
`--lid-velocity` 默认0.05；提取报告中的H/U/原点用于第二步绘图。

`--reuse-export` 只校验输出目录已有CSV/元数据并更新提取报告，不导出、不绘图；
不带该选项会调用选定的ParaView后端。掩码检查仍严格，内部缺口拒绝跨越插值。
原有 `report.json`、`comparison.png`、`reference_errors.csv` 作为历史结果保留，
提取入口不再生成或更新它们。需要新的参考比较时使用绘图入口的新输出路径。

绘图区需横纵轴等长时，给 `plot_centerline.py` 传入 `--square-axes`；它控制绘图区为正方形，不改变数据单位比例，默认保持既有排版。

## C 阈值涡量动画（job624717）

当前首选为 [Viridis 前15帧 GIF](images/gif-C-viridis-first15/job624717_C_viridis.gif)：
step3200..48000、t=25..375，每帧500ms、一轮7.5秒，1006×935，无限循环。
[完整40帧](images/gif-C-viridis/job624717_C_viridis.gif) 保留至step128000、t=1000（一轮20秒）。
两者沿用当前GUI场景：三维速度求旋度后切y=0.5，`mag(Vorticity)*1.0`，
Viridis固定0..1（超限饱和），无额外归一化、平滑或外部参考；加密框保留原白/红/绿配色。
裁剪保留用户拖动后的坐标指示、轴文字及完整色标，不缩放。
前15帧由下方长期合成入口直接选取原PNG生成；同目录保留裁剪PNG与来源JSON。

本次冻结场景、实际方向轴viewport、原PNG、命令、脚本快照和验证报告见
[`animation_C_viridis_current/`](../runs/a5_C_t1000_gif/job624717/C/figures/animation_C_viridis_current/)。
其中 `frames_final/` 是正式源帧；`frames_raw/` 和 `frames_axes_fixed/` 是保留的排错证据，
不能拿来合成最终GIF。本次源帧1834×1066，裁剪为 `(367,77,1373,1012)`。

[旧彩虹色40帧版本](images/gif-C/job624717_C_vorticity.gif) 与旧
[`animation_C/`](../runs/a5_C_t1000_gif/job624717/C/figures/animation_C/) 按历史保留，
不作为当前Viridis动画入口。C阈值完整数组0.25/0.5/1/2。

## C 中心线对比入口

- [仅AMR C与Cortes–Miller](results/C_t1000_Cortes/README.md)：保留原始两组对比图及数据。
- [AMR C、均匀512³与Cortes–Miller](results/C_uniform512_Cortes/README.md)：后续另存版本，来源和参考点解释以各自README/JSON为准。

- [最新：红色AMR B、AMR C、均匀512³与Cortes–Miller](results/BC_uniform512_Cortes/README.md)：B使用原t1000终态，不是续跑t2000。

这些图均为t1000，采用相同H/U标度及正方形绘图区，历次版本保留；不能由参考吻合推导时间收敛。

逐曲线指定颜色和线型可加 `--profile-style "AMR B" red -`，标签须与 `--profile` 一致，可重复指定其他曲线；未指定时保持原默认样式。

## GIF：选择帧段并保留坐标指示

长期入口为 `pack_animation_gif.py`；原 `scripts/temporary/pack_animation_gif.py` 为兼容软链，
历史命令仍可运行。复用已有合成逻辑并参数化选帧，避免为每个动画复制脚本。
输入须为同尺寸、从 `.0000.png` 开始连续编号的PNG序列。
`--start-frame` 从0计数，`--frames` 为帧数，省略则取剩余全部帧。
`--duration` 是每帧毫秒数，须为10的正整数倍；无限循环。
输出已存在时拒绝覆盖；裁剪PNG保存在输出目录的 `frames/`，应为新版本选择独立目录。

例如当前C/Viridis动画只取前15帧（t=25..375，每帧500ms，一轮7.5秒）：

```bash
case_dir=projects/3Dcases/Computing_performance_test/BOX3D_OSI
.venv/bin/python "$case_dir/data_post_processing/pack_animation_gif.py" \
  --input "$case_dir/runs/a5_C_t1000_gif/job624717/C/figures/animation_C_viridis_current/frames_final" \
  --output "$case_dir/data_post_processing/images/gif-C-viridis-first15/job624717_C_viridis.gif" \
  --prefix job624717_C_viridis --start-frame 0 --frames 15 \
  --crop 367 77 1373 1012 --duration 500
```

`--crop` 为原图像素坐标 left/top/right/bottom，右和下边界不含在内；不缩放。
示例只适用于本次1834×1066布局，新场景先检查坐标箭头、文字及色标的位置，
不能直接套用旧裁剪范围。全帧共用调色板；JSON记录每帧来源索引和SHA256。
共用调色板由缩略图量化得到，只占少量像素的细线颜色可能丢失；用 `--keep-colors 1f77b4 d62728`
把这些颜色固定写入调色板，合成后逐像素核对；省略时行为不变。
渲染时的方向轴位置恢复和首次相机复位修复已在原导出驱动中实现，
详见[本次导出记录](../docs/ai_changes/2026-11-04-C-viridis-current-gif.md)。

本次渲染驱动仍为冻结实验中的
[`render_remaining.py`](../runs/a5_B_three_plot3200/job622356/B/figures/animation_completion/render_remaining.py)，
新增的 `--view-source`、`--prefix`、`--axes-viewport` 保留当前显示和方向指示位置。
它仍检查40帧、t=25..1000，属于本组实验的复现驱动；不能把它当作任意时间序列的通用入口。
通用帧段合成使用上面的 `pack_animation_gif.py`，无需重算涡量。
