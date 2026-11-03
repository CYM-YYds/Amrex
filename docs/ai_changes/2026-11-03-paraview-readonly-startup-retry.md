# ParaView只读启动检查断线重试

依据：用户提供后处理脚本/launcher_20261103_092646_161a5c104c1b426287a4ec94adc0a243.zip。09:26:15、09:27:14失败于版本预检，09:26:35、09:27:04失败于hostname，09:26:46失败于client_manifest，均见Connection closed by 27.18.114.25 port 6022，无明确MaxStartups。这批日志证明SSH阶段中断，不能证明入口拒绝的具体原因，也不能据空版本输出判断版本不匹配。Authorized users only是SSH提示，Windows PowerShell5.1将stderr包装为NativeCommandError，不等于每条提示都是致命异常。09:06客户端日志使用旧的逐次SSH传输，不能作为新版长连接运行证据。

改动：扩展现有Invoke-StartupNative而非新增另一套重试机制，用ReadOnly显式标记安全查询/下载；仅这些操作对Connection closed/reset/timed out也有限重试，仍最多五次、4/8/16/32/60秒退避。GUI动作投递等有副作用的调用保持仅认证前MaxStartups可重试，服务启动命令保持不自动重放。认证与主机指纹错误优先停止。版本预检将SSH失败与版本不符分别报告；本次不改变桥接运行层或当前GUI。

验证：原生PowerShell回归扩充普通断线只读重试/有副作用不重试、主机指纹拒绝，以及成功命令stderr有登录提示仍保持stdout/退出码。当前无原生PowerShell，这项测试条件跳过，不能宣称Windows实机通过；执行PowerShell tree-sitter语法检查、Python测试语法检查和既有完整回归，结果见末尾。原始ZIP保留，不修改数据或用户场景。

结果：完整回归62项，49项通过、13项条件跳过；PowerShell tree-sitter与Python AST检查通过，按CRLF行尾规则检查diff通过。Windows5.1真实启动尚待验收，不宣称远端连接故障已解决。
