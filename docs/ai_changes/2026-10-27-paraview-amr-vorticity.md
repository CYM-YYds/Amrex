# ParaView AMR 粗层涡量覆盖掩膜修复

目的：解决读取细层后，即使 lev0 只有一个完整块，其未覆盖粗单元的涡量也出现矩形接缝的问题。
范围仅为 ParaView 桥接涡量管线、回归和使用说明；不改求解器、PlotFile 格式或现有 GUI 管线。

## 定位与实现

- BOX3D_OSI 的 `WriteVelocityFile` 调用 AMReX 26.06 `WriteMultiLevelPlotfile`；同步和异步
  写出均仅保存 valid 单元。输出前 `ComputeMacro()` 执行完整平均下传并重算各层速度。
- ParaView 6.2 的 AMR 读取器仍会生成覆盖粗单元的 `vtkGhostType=REFINEDCELL(8)`。
  这与文件中是否包含 ghost 不同。VTK 梯度代码通过 `GetCell` 获取单元中心，覆盖掩膜
  会影响该几何访问；“粗层有存储”不保证带掩膜的求导与完整粗层一致。
- 复用原生 `vtkGradientFilter`；AMReX 单元速度路径使用 ProgrammableFilter 在数据所在
  rank 的浅副本中，仅清除 REFINEDCELL 后计算旋度，随后恢复原掩膜。其他 ghost 位和输入
  保留；点速度路径仍用原 Gradient。代码随过滤器发送，不新增 Windows 模块安装依赖。
- 桥版本升为 6，支持既有热更新。已有 Gradient 不自动修改，新调用创建独立修复视图。

## 验证与边界

- 数据：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_256000`。
  lev0 为单块 128³，Cell_H 的 ghost 宽度为 0；物理坐标下直接比较 curl(u)，无 H/U 缩放。
  读取 level=1/2 后，503808 个粗单元标记为 8，lev0 速度与 level=0 逐点相同。
  原 Gradient 在未覆盖粗单元的最大分量差为 0.5712308129584454，24643 个单元受影响。
- 单 rank 和 4 rank 解析旋转场 `u=(-y,x,0)` 回归通过；检查旋度 (0,0,2)、输入与输出
  覆盖标记保持、DUPLICATECELL 保持、空块处理及原点速度管线。普通仓库桥回归
  24 项中 18 项通过、6 项因非 ParaView 解释器跳过。
- 真实数据 level=0/1/2 对照：修复后未覆盖粗单元的旋度与只读 lev0 逐点一致，最大分量
  差均为 0；输入和输出掩膜逐项不变。见 `fixed_reference_probe.json`。
- 真实三层 PlotFile 的 4 rank 计算、切片和渲染回归通过，进程正常退出；该检查不代表
  各层单/多 rank 的全部逐点等价。首次批处理因测试中的 `all` 被 ProgrammableFilter
  导入的 VTK 同名归约覆盖而等待；改为 `builtins.all` 后通过，诊断栈保留。
- Windows GUI 已热更新至版本 6；在现有 `plt_003200*`、物理时间 2000、读取层级 1
  上新增独立 y=0.5 修复视图，原管线保留。回执为 `gui_fixed_view.json`；尚无用户
  对图像效果的确认。`inspect` 现在回传读取层级，便于核对实际测试范围。

材料保存在 `<case>/runs/temporary/paraview_amr_gradient/`，为本次短诊断目录；保留输入路径、
复现脚本、数值 JSON、命令和测试日志。读取器 `plt_..` 指向已不存在的 `plt_259200`，
其加载失败不作为数值证据。

本修复依赖被覆盖粗单元速度有效，不补块间 ghost、不构造跨层差分；不保证细网格块边界
或物理边界的导数精度，也不证明求解器数值正确。现有数据的粗层对照不能替代网格收敛验证。
