# 固定预览与导出共同画布

目的：避免横向 GUI 预览与竖版高清导出的构图及字体比例不一致。

扩展已有 `publication_vorticity`，增加 `--image-size WIDTH HEIGHT` 和可选目标
`--view-id`。使用布局的原生 PreviewMode，限定单视图布局；导出与预览使用同一尺寸、
相机、字号和文字位置，不执行历史模式的临时高清字号切换。横画布保留左右留白，
四边标题及底部色标围绕图面定位。未指定尺寸时保留历史行为。

验证：普通 Python 29 项通过（跳过 9 项 ParaView 专项）；编译节点 ParaView 6.2.0
解析旋转场排版回归通过，2066×1066 导出与返回后直接截图逐字节相同，PreviewMode
和 ViewSize 均为目标尺寸，原旋度与缩放结果检查通过。退出时 EGL 有活动纹理警告，
测试返回 0。此项验证不代表求解器数值正确性验证。

Windows GUI 现场完成：仅更新新增视图 18084，原视图 14984 保留。导出后再次 inspect
确认视图仍为 2066×1066。step=256000、time=2000、lev2，原始旋度范围
[1.77e-15,25.6]，固定显示 0–1。覆盖用户指定的
BOX3D_OSI `runs/20261019_124153_job615364/figures/vorticity_slice3_reference_style_20261027.png`，
同步同名 JSON/MD；旧 PNG/JSON/MD 保留为 `*_previous_2400x2640.*`。
现场 GUI 和 PNG 同尺寸；自动回归中的逐字节对照不等同于跨显示器 GUI 截屏逐像素验收。
