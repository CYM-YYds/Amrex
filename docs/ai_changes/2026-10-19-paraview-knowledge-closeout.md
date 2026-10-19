# ParaView 6.2.0 知识收尾

## 范围与当前状态

覆盖 ParaView skill、6.2.0 GUI 服务入口、Windows 版本检测修复及直接消费方。
使用方式唯一入口为 [ParaView skill](../../.agents/skills/paraview/SKILL.md)，本记录只保存验证状态和保留现场。
本轮未修改执行脚本、数值路径、全局配置或运行数据。

| 事实面 | 状态 | 依据与边界 |
|---|---|---|
| 代码 | verified-current | Windows 文件版本资源优先、pvpython 回退；远端 6.2.0；GUI 单进程、编译节点保护；9 项既有测试与 PowerShell 语法树检查通过 |
| 运行态 | verified-current | 新桥实际 inspect 成功，本次回执 ID/URI/能力与源码 SHA256 符合仓库；服务进程在 whshare-agent-1，实际 EXE 为独立环境 pvserver、监听 11115；本机完整验收边界见下文 |
| 文档 | changed-and-verified | 消除“截图完全未验证”的旧说法；区分 batch、Linux client/server 与 Windows GUI；补充 PS1 保留报错和空版本输出处理 |
| 规则 | verified-current | 根 AGENTS.md → CLAUDE.md、.claude/skills → .agents/skills 及两个旧连接兼容软链有效；未增加重复规则 |
| 记忆 | out-of-scope | generated-read-only；只读检索未命中 ParaView 条目，未写生成索引或 correction input |
| 工作区 | pending | 起始已跟踪文件无改动，只有一个 detached worktree；本轮仅提交文档。忽略的验证目录保留待用户确认，未清场 |

## 验证证据与限制

- 用户反馈的首个 6.2.0 Windows 启动错误定位到 GUI --version 空输出。修复提交 1644dfd 改用文件版本资源/同目录 pvpython；本轮实际核对当前实现和 PowerShell 语法树，没有重复修改启动逻辑。
- 本轮实时 inspect 返回四项能力、cs://localhost:11115 和已加载的读取器及两个 Slice。新的注册哈希与仓库桥源码一致；回执属于同一启动编号。
- 实际服务 EXE 来自 .tools/paraview-batch/env/bin/pvserver；现有 paraview-config.cmake 明确 PARAVIEW_USE_MPI=OFF，不能套用旧 32-rank 命令。
- 保留的 6.2.0 client/server 实测证据包含 320×240 非空 Sphere PNG、成功导出日志、1025 个有效点；9 列数值与正式 batch CSV 完全一致。该临时测试服务日志以 Exiting 结束，不与用户当前现役服务混淆。
- 门禁：PowerShell tree-sitter、三份 Bash 语法、9 项测试、skill validator、受影响 Markdown 链接、PS1 BOM/CRLF、兼容软链及差异空白检查。
- pending：尚未直接读取用户 Windows PE 资源值或最终 Bridge ready 控制台输出；宿主重新发现后的技能列表展示未核实。现有在线证据不能推导这些检查的所有分支均通过。
- warning：离屏渲染时仍有 DISPLAY 为空的 X 探测警告，输出图像和数据正常。没有 PowerShell/Windows 执行环境，语法树检查不能代替平台运行验证。
- out-of-scope：动画、Windows GUI 截图、MPI 并行扩展、远端推送/PR、其他算例与全 workspace 内容审计。

## 清场预览与已完成动作

- 当前候选仅 .tools/paraview-batch/gui-smoke/：client.py、client.log、server.log、sphere.png、centerline.csv，共 5 个文件，120683 字节。属于本次一次性测试与复核证据，仍保留；须完整汇报后再获用户确认才能删除。
- .tools/paraview-batch/env、显式包锁、兼容软链、正式 CSV、运行目录、检查点及现役服务均有用途，不列为清理候选。
- 此前用户已确认删除的 skill-migration-check/ 已不存在；8 个依赖缺失输入的 stage 提交入口已于 b39f4f9 删除。两张 PNG 的用户删除已单独提交于 0645467，不属于本轮修改。
- 旧实现和阶段性验证结论保存在 Git 及对应 ai_changes 历史记录中，不再作为现役使用说明。
