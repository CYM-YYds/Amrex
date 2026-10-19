# ParaView 6.2.0 GUI 桥接入口

## 目的与范围

用户已在 Windows 安装 D:\Software\ParaView 6.2.0\bin\paraview.exe，连接入口改用远端独立 6.2.0 服务。
修改限于 ParaView skill 的启动脚本、节点保护、测试和说明；旧后处理目录软链继续指向同一实现。

## 主要改动

- PS1 默认使用用户给出的 6.2.0 路径，启动前核对本机及编译节点 pvserver 的版本，拒绝误选旧版。
- 移除旧系统模块和 32-rank mpirun 路径。已安装构建的 paraview-config.cmake 明确 PARAVIEW_USE_MPI=OFF，因此使用单进程服务。
- 新增 run_paraview_server.sh，限定 whshare-agent-1 并清理系统 Python、动态库及图形插件路径。独立封装服务生命周期入口，不改批处理导出入口的职责。
- 外部 SSH 入口已在编译节点时直接运行、隧道指向 localhost；否则转发到编译节点。端口检查、监听等待和服务启动采用一致路由，命令以 Base64 传过两层 Shell。
- 保留自动下载桥客户端、固定共享注册目录及 GUI 启动专属回执校验。桥协议和 Python 客户端实现没有变更。

## 验证与边界

- 编译节点实际启动新版服务，pvpython 6.2.0 成功连接、渲染 Sphere 并输出 320×240 非空 PNG。
- 同一 client/server 会话读取 job613495/plt_256000 并复用中心线导出函数：1025 点全部有效，9 列数值逐点与原 batch CSV 一致。测试服务在 finally 中退出，无长期测试服务遗留。
- 日志出现无 X 显示的探测警告，但离屏截图与导出均成功。此证据不替代 Windows 官方客户端到当前构建的现场验收。
- 检测到编译节点 SSH 回连自身超时，故增加直接运行分支；对应命令在真实编译节点验证输出 hostname 与 6.2.0 版本。
- 9 项协议、路由及生命周期测试通过，包含错误节点拒绝启动、编译节点不回连自身和转发失败不本地执行；Bash 语法、skill validator、文档链接、PS1 BOM/CRLF 与差异检查通过。
- 本环境没有 Windows PowerShell/ParaView；未完成 PS1 的 Windows 执行验收。外部 SSH 入口到编译节点的跨节点分支通过模拟路由验证，真实主机链仍以用户运行结果为准。
- 现场证据保留在忽略的 .tools/paraview-batch/gui-smoke/，没有覆盖用户窗口或既有服务。

## 用户操作

重新下载新版 connect_paraview.ps1 为本机普通文件，保存工作并关闭占用 11115 的旧连接，再运行该文件。
旧本机 PS1 不会自动升级自身。成功以本次 Bridge ready、版本/哈希/连接检查全部通过为准。
