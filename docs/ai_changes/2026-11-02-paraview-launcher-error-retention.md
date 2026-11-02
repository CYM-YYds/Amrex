# 连接启动异常留存

用户反馈新版连接脚本启动后最初的 PowerShell 窗口立即关闭。当前尚未取得 Windows 原始异常，不能归因于上一版的 stdout/stderr 重定向或某个具体语句。

改动：在 param 后增加脚本级启动异常处理与停止错误策略。失败时显示原始错误，保存完整 ErrorRecord 及 ScriptStackTrace 至脚本旁 paraview_logs/launcher_error_时间_编号.log；写入失败时退回系统临时目录。交互式 ConsoleHost 且 stdin 未重定向时等待回车后重新抛出原错误，保留非零退出；增加 NoPauseOnError 供自动化关闭暂停。成功路径不增加暂停，已有服务窗口及 GUI 不会因错误处理被主动关闭。参数绑定、整个文件解析错误不能由脚本 trap 捕获，文档保留 pwsh -NoProfile -NoExit 排查入口。

验证：PowerShell tree-sitter 解析无错误节点；既有桥接测试通过。新增两项原生 pwsh 回归分别验证异常退出/日志内容，以及本地日志目录无法创建时回退临时目录；本机无 pwsh，两项跳过，不能宣称原生运行通过。官方便携版下载连接超时，未绕用其他 Python 环境。Windows 实机原始异常仍需用户在保持打开的控制台中回传。
