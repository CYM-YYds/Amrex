# ParaView 桥接投递与出图失败恢复

目的：保持现有 GUI/批处理分层，减少协议与配置漂移，保证并发投递不会覆盖命令，
并使原位排版发生 Python 异常后恢复用户已有场景。范围为 ParaView skill、根 README 和本记录。

## 主要改动

- 将已有动作与字段表移到内部 `paraview_protocol.py`，能力列表从表派生。
  新模块解决投递器和客户端分别维护协议声明的问题，不加载 ParaView，也不新增用户入口。
  打包器静态嵌入协议与操作模块，生成 Windows 单文件客户端（版本 18）。
- 投递器以会话级 `flock` 保护检查、写入和回传等待，竞争调用立即拒绝。
  锁文件保留，进程退出自动释放；超时后的未决命令仍阻止下一次覆盖。
- PS1 只维护一处仓库根路径，服务脚本由其派生；客户端路径和桥目录来自远端 manifest。
  投递器与 GUI 均读取 `PV_BRIDGE_ROOT`，GUI 从启动配置的 `PV_BRIDGE_CLIENT_SOURCE`
  下载热更新源码，不接受命令中的源码路径。手动加载及旧启动器保留旧固定路径兼容。
- 出图模块增加局部属性备份生命周期，复用 ParaView 原生属性 Copy，不加载另一套场景。
  异常时恢复已有标量、标题、相机、布局、显示、色标与颜色映射引用，回收本次新增对象。
  单独处理 scalar_bars 注册组、空属性向量及未 Apply 的属性值；旧标量不因数据范围失败被删除。
  恢复不完整明确报错，不保证服务崩溃/断线后的恢复，也不撤销已经写出的 PNG。
- 保留现有求导与中心线算法、批处理入口、单文件分发和热更新所有者生命周期。

## 验证

- 仓库 `.venv/bin/python`：Python AST 语法检查、生成物 `--check`、完整 unittest 回归。
  47 项中 37 通过、10 项因普通 Python 无 ParaView 跳过。
  新增独立进程争用、进程退出释放锁、超时保留命令、匹配回执后继续投递、配置目录及
  启动配置指定源码路径的测试。
- PowerShell 使用仓库已有 tree-sitter-powershell 解析，语法通过；保留原 UTF-8 BOM/CRLF。
  `git -c core.whitespace=cr-at-eol diff --check` 检查本次差异。
- 编译节点 `whshare-agent-1`、同一 `.tools/paraview-mpi/env`：
  单进程 `pvpython --force-offscreen-rendering` 运行 `test_vorticity_pipeline.py -v`，
  10 项中 8 通过，2 项因未指定真实 PlotFile/MPI rank 跳过。
- 同安装 `mpiexec -n 4 pvbatch --force-offscreen-rendering`，设置 `PV_EXPECT_MPI_RANKS=4`：
  MPI rank 数、出图失败恢复、解析旋转场及出图/预览一致性三项全部通过。
  正常出图解析场仍为 curl=(0,0,2)，H/Uref=20 后标量为 40。
  失败注入覆盖首次创建和重复排版的截图异常，以及修改布局后的斜切片验证失败；
  逐项比较对象注册、显示、色带引用、相机、布局与已有标量，核对未 Apply 值。
- 真实运行仍出现既有 printf 色标格式和部分 MPI EGL texture 退出警告，测试退出码均为 0。
  未连接、热更新或修改用户 Windows GUI；未重新验收 Windows 启动、真实 PlotFile 或数值精度。

复现：

```bash
.venv/bin/python .agents/skills/paraview/scripts/build_paraview_client.py --check
.venv/bin/python -m unittest discover -s .agents/skills/paraview/scripts/tests -v
# 以下在 whshare-agent-1，从仓库根运行；单进程用 pvpython 替换 mpiexec/pvbatch 并去掉三个测试选择项。
env -u LD_LIBRARY_PATH -u PYTHONPATH -u PYTHONHOME -u DISPLAY \
  -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH -u PV_PLUGIN_PATH \
  QT_QPA_PLATFORM=offscreen PV_EXPECT_MPI_RANKS=4 \
  .tools/paraview-mpi/env/bin/mpiexec -n 4 .tools/paraview-mpi/env/bin/pvbatch \
  --force-offscreen-rendering .agents/skills/paraview/scripts/tests/test_vorticity_pipeline.py \
  VorticityTests.test_mpi_controller_has_requested_process_count \
  VorticityTests.test_publication_failure_restores_existing_objects_and_layout \
  VorticityTests.test_solid_body_rotation_and_original_view_preserved -v
```

新增脚本均为内部模块或长期回归，不是一次性脚本。测试场景和截图由测试临时目录自动回收；
本记录保留命令、验证结果与边界，不清理历史运行、图表或用户修改。
