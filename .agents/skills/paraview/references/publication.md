# 涡量切片出图

## 保留手动编辑，只更新对齐

已修改标题、字体或色标位置时，不要重跑会应用标准样式的 `publication_vorticity`。
使用已有四边标题和水平色标：

```bash
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current align_publication \
  --source Slice3 --view-id '<目标视图ID>' --timeout 180
```

保留文字内容、字体、相机、色带、色标标题与竖直位置；重新按文字包围盒居中四边标题，
距图框统一 40 像素，将水平色条左右端点对齐图框。色标标题及数字可能伸出色条端点，
对齐目标是彩色条，不是全部注释的总包围盒。可选 `--output /absolute/shared/check.png`
按当前视图尺寸导出。操作不新建管线；失败恢复本次属性改动。

对已有涡量切片做论文/参考图排版，或为 Z 轴标题新建独立页面；连接、状态与热更新见 [GUI 桥接](bridge.md)。

## 从 GUI 场景导出时间序列

用户已手动调整的场景以当前 `.pvsm` 和目标视图为准，不从旧 Python trace 近似重建。
先用 [`snapshot_state`](bridge.md#保存-gui-场景供批处理复用) 保存场景，记录视图 ID、
`ViewSize`、实际输入列表、物理时间、旋度定义及显示范围，再按[批处理说明](batch.md)在编译节点导出。

- 保持目标视图原始像素尺寸，使用 `FontScaling='Do not scale fonts'`；改画布比例或自动放大字号
  会改变图面占比、色标标题与图框的相对位置。不要再套用另一种排版。
- 时间帧可分配给相互独立的单进程 `pvbatch`，每个任务独立加载场景并写不同文件；核对内存与
  节点资源后选择并发数。逐帧核对输入、物理时间、切片非空、有限值及 AMR 字段，保留日志与状态。
- 动态框线使用[显式 AMRLevel 字段](bridge.md#使用层级字段生成动态-amr-框线)，
  不依赖 `vtkBlockColors` 或单帧块颜色属性。MPI Fetch 的外层聚合分组不是 AMR 层号。
- 要减少左右白边，在原尺寸渲染完成后，对全部帧采用同一个裁剪框；保留未裁剪原图，
  检查裁剪区域像素一致。不要通过改渲染视口来代替裁剪。
- PNG 作为原始图像保留；GIF 是调色板动画预览，记录帧序、每帧时长与循环方式，
  不能把其颜色量化结果作为原 PNG 的逐像素数值证据。

## 独立 Z 轴标题视图

原生 Z 轴标题与刻度重叠时，可保留原页面，新增独立布局和两个 Text 管线标题：

```bash
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current add_axis_title_view \
  --source Slice3 --timeout 600
```

先选中该 Slice 可见的目标视图。新视图复用切片，复制相机、轴与显示设置并使用独立色标；
仅在新视图清空原生 Z 标题，新增左右 Text 标题。原视图、输入连接和色标保持原样。
选中新增 `Slice3_Z_title_left_*` 或 `right_*`，在 Properties 修改 Text，在 Display
修改 Position、Font Size 和颜色。标题位置按创建时的相机计算，之后缩放或旋转时可能需要
手动移动。保存 `.pvsm` 可保留新布局和文字；此操作不重新计算涡量。

误移动独立页面后，使用其 inspect 返回的视图 ID 复位相机：

```bash
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current add_axis_title_view \
  --source Slice3 --reset-view '<视图ID>' --output /absolute/shared/reset.png --timeout 600
```

复位按切片边界居中并正对截面，重新定位左右标题；不重建节点、不改色标或文字内容。
可选 PNG 按当前视图尺寸导出，确保画布宽高比与屏幕一致；参考样式的 2400×2640
竖版导出与横向 GUI 的图面占比和留白不同，不表示数据坐标比例被改变。

## 论文与参考图排版

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
发生 Python 异常时恢复操作前的场景属性并回收本次新增对象；已有标量、标题和显示对象
原位保留，包括未 Apply 的属性值。恢复边界与失败报告见 [实现与维护](maintenance.md#实现边界与验证)。

需要白底、四边坐标、Arial 字体和底部端点色标的参考图样式时，增加 `--style reference`：

```bash
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current publication_vorticity \
  --source Slice3 --style reference --vorticity-scale 1 --color-range 0 1 \
  --output /absolute/shared/output/vorticity.png --timeout 600
```

该样式导出 2400×2640 PNG，隐藏目标视图内其他可见几何叠加（保留管线），
刻度间隔为范围的 1/10。默认 `--style publication` 仍使用上述 Times 与方形画布。
屏幕字号按应用时的窗口大小缩小；导出时临时使用高清字号，成功后恢复本次排版的屏幕字号。
重新启用原 Slice 后也可再次执行，命令会重新选择正确标量显示，并恢复 Calculator 的
`mag(Vorticity)*乘数` 表达式及未应用编辑值。`normmag` 不是支持的函数，`norm` 与 `mag`
不能直接拼接。inspect 会回传已应用/未应用表达式和可见节点，便于区分旧错误记录与当前状态。
示例保留原始旋度，以 0–1 显示并对超出部分饱和，不代表除以最大值归一化。
坐标标题按真实截面选择，y 法向截面为 x/H、z/H，不照抄二维参考图的 X、Y。

需要 GUI 预览与导出使用同一画布和字号时，指定共同尺寸：

```bash
.venv/bin/python .agents/skills/paraview/scripts/paraview_bridge_send.py current publication_vorticity \
  --source Slice3 --view-id '<目标视图ID>' --style reference --image-size 2066 1066 \
  --vorticity-scale 1 --color-range 0 1 --output /absolute/shared/output/vorticity.png --timeout 600
```

要求目标布局仅含一个视图。GUI 中退出 PreviewMode，恢复正常铺满视图区；以当前实际
ViewSize 作为排版和导出的共同尺寸。`--image-size` 在批处理中设置布局尺寸，GUI 中只作为
请求记录，不强制缩放窗口；回执同时给出 requested_image_size 与 matched_view_size。
导出不临时放大文字，也不更改相机或文字位置。横画布的左右留白保留，标题、底部色标
按图面位置安排。未指定 `--image-size` 时保留历史导出行为。

同尺寸参考排版的四边标题根据图框屏幕投影和 VTK 字体包围盒重新定位：上下标题水平
居中，左右标题竖直居中，文字包围盒边缘到图框统一 40 像素。该间距以图框为基准，
不是自动探测原生刻度标签的外缘。改变相机、窗口或字号后重新执行命令更新，不是实时跟随。
