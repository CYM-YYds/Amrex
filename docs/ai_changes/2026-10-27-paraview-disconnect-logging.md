# ParaView lev2 断线诊断与服务日志留存

目的：用户在 Windows GUI 将读取层级从 lev1 改为 lev2 后出现 `Server disconnected`，
Output Messages 显示 Tag mismatch；用户关闭窗口，旧启动器未保存服务输出，无法追溯首错。
本次只增加服务日志留存，不宣称已修复断线；不改求解器、读取层级或涡量数值算法。

## 已确认与边界

- 查询时用户的 pvserver 已退出，控制桥最后没有待执行的数据修改命令。
- 本机 ParaView 6.2 安装头文件和对应源码中，188968 为 `vtkPVProgressHandler::ABORT_TAG`，
  55630 为 `vtkPVSessionServer::EXECUTE_STREAM_TAG`。报错显示取消检查与普通命令通道错配，
  不能据此认定用户点过取消、发生 OOM，或已确定错配的触发原因。
- 使用同一份 `runs/20261019_124153_job615364/plt_256000`，在 whshare-agent-1 的独立随机
  端口启动 4 rank pvserver，由 Linux pvpython 客户端连接。原生 Gradient 和修复管线
  各自完成 lev1→lev2→lev1→lev2 的读取、切片与渲染，退出码 0，未复现 Tag mismatch。
  两套管线同时连接同一读取器也完成相同切换，退出码 0；单套 lev1→lev2 约 16–19 秒，
  双套约 37–41 秒，仅为本次诊断观察，不是性能基准或 Windows 完成时间承诺。
  Linux 客户端不包含 Windows GUI、原场景及其取消/进度交互，因此不能排除这些条件。

## 修改与验证

- `run_paraview_server.sh` 正常启动自动将 stdout/stderr 同时显示并保存到
  `.tools/paraview-mpi/logs/pvserver_日期时间_随机后缀.log`，头部记录节点、进程数和前缀。
  使用 exec 保留服务退出码；版本预检不生成日志。日志不自动清理。
- 原 Windows PS1 每次 SSH 调用远端服务脚本，因此此次修改不要求重下载 PS1。
  新日志仅覆盖服务输出，不包括 SSH 客户端故障、Windows Output Messages 或整个节点被杀时
  来不及输出的信息；不可承诺任何断线都能从该日志直接定位。
- `bash -n`、Python 语法解析通过；桥回归 25 项中 19 通过、6 项因普通解释器跳过。
  新回归验证双输出流留存、带空格日志目录、服务失败码 23 原样返回，以及版本预检不产生日志。
  双管线真实四进程服务已验证日志路径打印与共享文件留存，客户端和服务均正常退出。

本次短诊断结果与保留脚本集中于
`projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/paraview_amr_gradient/lev2_disconnect/`。
保留客户端/服务端日志、退出码、输入路径和复现脚本。未取得本次 Windows 断线前的服务日志，
根因仍未确定；下一次正常连接即可自动留存服务端证据。
