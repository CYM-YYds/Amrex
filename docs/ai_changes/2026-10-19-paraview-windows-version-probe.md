# 修复 Windows GUI 版本预检空输出

用户现场运行 6.2.0 启动脚本，在本机版本检查处报 Expected local ParaView 6.2.0; got: 空值。
此前直接捕获 paraview.exe --version 的输出，Windows GUI 程序不一定将该输出写入 PowerShell 管道，因此不能据此判为版本不匹配。

本次改为优先读取所选 EXE 的 ProductVersion/FileVersion 资源，并将四段版本归一为三段。
版本资源缺失时使用同安装目录的 pvpython.exe --version；无法识别或版本不匹配仍明确停止。
移除 GUI 版本探测进程，远端版本、编译节点限定和桥接回执校验保持原有逻辑。

验证：版本提取正则覆盖三段、四段及后缀形式；PS1 BOM/CRLF、文档链接和差异检查通过；既有 9 项测试通过。
本环境没有 PowerShell/Windows，实际 PE 版本资源和本机预检通过仍需用户现场验证，不把静态检查视作整套 GUI 启动成功。
