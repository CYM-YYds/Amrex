# 桥接初始化 SSH 中断的有限重试

证据：用户提供 `后处理脚本/pvserver_20261102_152818_port11115_705ef25a62e94ac0929d50f0e122aced.zip`，含 15:28 单 rank 与 15:29 八 rank 两次启动材料。两份 client.stdout.log 均为 Windows SetTimer 后紧接 `ParaView bridge startup failed: Connection closed by 27.18.114.25 port 6022`；stderr 为空，无 bridge.json。对应远端服务日志均记录 Client connected，无本次 Tag mismatch。因此可定位为桥接初始化 SSH 被关闭，而非 pvserver 没有连接；尚无 SSH 服务端日志，无法判定关闭是限流、网络还是其他入口策略。

实现：版本 23 复用运行层 remote 调用，将首次建目录与注册合并为一次 SSH。仅 SSH exit=255 且错误包含连接关闭、重置或连接超时，或 subprocess 超时，才对安全可重入的初始化命令最多三次尝试（等待 2、4 秒，可被 stop 中断）。目录创建改为可重入，注册仍原子替换同一会话数据。认证和指纹错误不重试，后续 GUI 命令不新增重放机制。初始化重试和最终错误及时 flush 到客户端日志。

验证：55 项测试通过，12 项因环境条件跳过；新增回执丢失后真实 shell 重试注册、三次上限、认证不重试及 SSH 失败分类四项回归。生成单文件客户端 --check 与 Python AST 检查通过。未宣称完成 Windows 多次启动验收；原始 SSH 关闭原因及此前 GUI Tag mismatch 仍未解决。

交付：启动 PS1 下次自动下载客户端 23；用户无需单独下载 Python 客户端。原压缩包与现有日志保持不变，不重启当前 GUI，不修改求解器。
