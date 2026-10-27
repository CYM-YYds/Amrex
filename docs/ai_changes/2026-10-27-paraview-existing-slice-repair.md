# 原位修复现有 Slice3 的 AMR 涡量

目的：用户希望保留现有 Slice3 的切片与展示，修复被覆盖粗单元标记导致的涡量误差，
而不是另建视图。范围为 ParaView 桥操作、投递器、回归与说明；不改求解器或文件输出。

实现：新增白名单操作 `repair_vorticity_slice --source Slice3`。先验证目标唯一、直接上游
是原生 Gradient、其输入是三维单元速度向量，再复用既有 AMR_CELL_VORTICITY_SCRIPT，
按原数组名称计算旋度并恢复覆盖标记。只重接指定切片的 Input；原 Gradient 和其他分支保留，
不重建切片、显示、相机或色标。异常时恢复原连接。inspect 增加输入代理 ID 便于核对管线；
桥版本升为 7，可热更新，不增加任意代码执行入口。

验证：

- Python 语法、针对文件的 diff 检查通过。普通 Python 桥回归 26 项中 19 通过、7 因解释器跳过。
- 编译节点单 rank ParaView 回归 7 项中 5 通过、2 因未指定 MPI/真实数据条件跳过；包含
  解析旋转场、覆盖掩膜保持与新测试：原切片的 ID、平面、显示对象、着色数组、相机及旋度保持。
- 四 rank 运行新切片修复测试通过，正常退出。
- 当前 Windows 连接中，`plt_003200*` 读取层级为 2，物理时间 2000（step=256000）。
  原连接为 Calculator1(14256) → Gradient1(14521) → Slice3(14535)。已成功接入
  Slice3_AMR_vorticity(20674)，复用 Calculator1 的 Result 单元向量；Slice3 仍为 14535，
  更新其原有显示视图 14765、15492，旋度模范围有限，为 [0,25.600000000000055]。

回执与日志保存在
`projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/paraview_amr_gradient/slice3_repair/`，
保留用于复查。此操作沿用此前已逐点验证的覆盖掩膜修复；不补充缺失的块间 ghost，不保证
细层块边界导数精度，也不代表此前交互断线已修复。当前图像效果仍待用户确认。
