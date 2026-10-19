# 固定 ParaView 控制桥入口并接入启动脚本

- 目的：后续运行连接脚本即可自动建立远端连接与控制桥，免去手动加载 Python 和复制随机会话目录。
- 范围：后处理目录的启动脚本、桥客户端、投递器及说明。保留并整合用户已有的 Liuzhaohui 账户配置；工作区原有文件模式变化不纳入本次提交。
- 改动：本机桥脚本固定在启动脚本旁，每次 SCP 同步超算仓库最新版；检测固定本机端口占用；远端监听就绪后用 --url 自动连接、--script 自动加载。SSH 配置通过子进程环境传入并恢复父进程环境，下载失败停止且保留旧文件，生成 CMD 使用无 BOM UTF-8。
- 会话：使用共享家目录 ~/.cache/paraview-bridge 下独立 UUID 会话和原子 current.json 注册，旧会话不覆盖新注册；原子心跳用于拒绝过期入口。投递器默认发现 current，保留显式旧 /tmp 会话路径兼容。客户端 SSH 超时改为 45 秒并输出错误详情，投递等待 60 秒。
- 验证：仓库 .venv Python 语法解析通过；PowerShell tree-sitter 语法树无错误（当前节点无 pwsh，未宣称原生 PowerShell 执行通过）；抽取生成的 shell 命令 bash -n 通过；模拟 GUI 加真实文件协议的注册、投递/回传、同 ID 去重、0700 权限、过期心跳/非法注册拒绝、CLI 默认/显式入口通过；文档路径、BOM/CRLF 与暂存 diff 检查通过。实测共享家目录在当前节点和 whshare-agent-1 可见，内部 SCP 中文源路径与含空格目标路径通过。
- 边界：没有启动新的 pvserver 或修改当前 ParaView 管线。Windows 完整自动启动仍需用户下载新版 PS1 后验证；源码确认 ParaView 5.11.2 支持先处理连接再执行脚本。控制桥当前仍仅开放 inspect、background、restore_background，不自动重建旧数据管线，不宣称已开放任意数据分析。
