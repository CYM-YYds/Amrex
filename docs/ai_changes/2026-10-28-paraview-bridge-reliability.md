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

## 知识收尾核对（2026-10-28）

| 事实面 | 状态与证据 |
|---|---|
| 代码 | `verified-current`：实现提交 `793c9ab`，协议/操作源码与两份生成物的 `--check` 一致。 |
| 运行态 | `pending`：实时 `client_status` 返回连接正常、GUI 客户端版本 16、仓库版本 18、`client_update_required`，支持热更新；本次仅 inspect，不执行升级或重建管线。Windows 新启动流程仍未重新现场验收。 |
| 文档 | `changed-and-verified`：修正用户出图说明中的失败恢复语义，维护入口补本记录指针；路径与链接核对通过。 |
| 规则 | `verified-current`：根 `AGENTS.md` 为有效的 `CLAUDE.md` 软链；本作用域无新增覆盖规则，兼容入口软链有效，无需修改规则。 |
| 记忆 | `pending` / `generated-read-only`：生成索引仍含手改客户端和仅单进程的旧说法；当前维护说明、安装器与源码为权威。本次只读核对，未改生成记忆或创建纠正输入。 |
| 工作区 | `verified-current`（本次范围）：单一 worktree，无新增一次性脚本或待清场分支；两个求解器源码修改及 `test.pvsm` 删除为 `out-of-scope`，保持原状。 |

已机械枚举仓库 467 份 Markdown（跳过盘点脚本定义的依赖/构建目录），按本次 ParaView 范围检查现役文档、
下游引用和兼容入口；不逐篇审查其他算例及历史变更记录。历史锁文件、运行日志、桥会话和结果保留，
不按年龄或未引用判废，无本次可删除候选。代码验证沿用上节证据，不因纯文档修正重跑数值实验。
现役使用说明以 skill 的主题文档为准，旧记录中的运行状态仅代表记录时点。
遗留 warning 为上节已记录的 ParaView printf/EGL 提示；全仓库 diff 检查另在范围外
`Kernels.H` 的已有修改中报告 CRLF 行尾空白，本次不处理；三份收尾文档的范围内 diff 检查通过。
