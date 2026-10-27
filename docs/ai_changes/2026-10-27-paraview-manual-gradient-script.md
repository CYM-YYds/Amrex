# 用户自行执行的 AMR Gradient 修复脚本

目的：用户先创建原生 Gradient，再自行加载一个脚本生成修复节点，随后手动添加 Slice 等。
现有桥操作面向远端投递，不能替代不依赖桥接的单文件 GUI 入口，因此新增长期工具
`后处理脚本/repair_amr_gradient.py`（软链到技能 scripts 下的生成物），并更新使用索引。

## 实现与归属

- 选中原生 Gradient 后在 ParaView Python Shell 运行单文件，生成独立的
  `Gradient1_AMR_fixed` 等 Programmable Filter 并选中；输入接原三维单元数组，
  不依赖旧 Gradient 的求导结果，不改原节点、消费者、切片、显示或相机。
- 继承四类输出勾选项、输出数组名及原生近似/贡献/替代值选项。支持原生梯度、旋度、
  散度及 Q；向量派生量要求三分量，当前入口拒绝点数据和非三维输入。
- 掩膜求导算法仍以桥客户端 `AMR_CELL_VORTICITY_SCRIPT` 为唯一源。独立文件由该常量与
  `templates/repair_amr_gradient.py.in` 打包生成，运行时无外部文件依赖；生成物同步测试
  比较完整文本，维护者通过已有 sender 的本地 export_gradient_script 操作重建。
- 四进程测试发现无数据 rank 的占位 `vtkDataObject` 不具备 GetCellData；唯一算法中
  增加 vtkDataSet 类型判断并同步生成物，避免空分区 Python 报错，不改变有效网格的求导。
- 单文件用 Python 内置 any/all，避免 ParaView ProgrammableFilter 导入的同名 MPI 归约
  改变客户端校验语义。用户仍在原 Gradient 上修改选项，然后重新生成修复节点。

## 验证与边界

- 单文件生成一致性、Python 语法及文档链接检查通过；普通 Python 回归的 ParaView
  依赖项按条件跳过，28 项中 20 通过、8 跳过。最终四进程全套 8 项通过，退出码 0，
  包含真实输入；日志无 Python traceback 或 VTK ERR，现有 OpenGL 资源析构警告仍保留。
- 带 64 个 REFINEDCELL 的解析旋转场 `u=(-y,x,0)`：检查全部单元的梯度矩阵、
  旋度 (0,0,2)、散度 0、Q=1，自定义数组名继承，覆盖标记不变，原 Slice 仍连接旧 Gradient。
  空占位对象单独回归，避免将退出码 0、root rank 数值通过误当作所有 rank 都无报错。
- 测试夹具在各 rank 分别生成，测试结束重置会话，防止根 rank 独占数据、同名切片和
  前一场景残留污染组合回归；没有关闭测试或降低数值断言。单文件自动执行入口另行通过。
- 真实输入为 job615364 的 `plt_256000`、读取层级 2，使用四进程 ParaView 环境验证
  独立入口生成结果与原有管线；不操作用户当前 GUI。Windows 手动 Run Script 的验收
  由用户下载后完成，不将编译节点批处理当作 Windows 操作验收。

输入路径、测试日志和首次空分区错误日志保留在
`projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/paraview_amr_gradient/manual_gradient/`。
本脚本仍依赖覆盖粗单元速度有效，不补缺失 ghost，不提供跨层差分或断线修复。
本次不改 Windows PS1，不自动修改当前窗口，也不提交用户的求解器改动。
