# ParaView skill 知识收尾

## 范围与状态

本次按 neat-freak 核对 ParaView skill、现有入口和直接消费方，不扩大到求解器、其他算例或全局配置。
仓库库存脚本已机械枚举 Markdown（434 篇）；只全文审阅规则、索引及与本次变更相关的文件。

| 事实面 | 状态 | 依据与边界 |
|---|---|---|
| 代码 | verified-current | 核心实现与调用路径一致；本次未修改执行脚本或数值路径 |
| 运行态 | pending | 当前桥实际 inspect 成功，版本 2、四项能力、URI 正确，但注册源码哈希不同于仓库；完整 Windows 启动及 GUI 服务实际执行节点仍待核验 |
| 文档 | changed-and-verified | 桥接说明移除重复的阶段叙事，合并为当前操作、状态所有者、故障与验证边界；根目录与旧目录说明指向 skill |
| 规则 | verified-current | 根 AGENTS.md → CLAUDE.md、.claude/skills → .agents/skills 有效；没有新增重复规则或改全局配置 |
| 记忆 | out-of-scope | generated-read-only；只读检索未命中 ParaView 条目，未写生成索引或 correction input |
| 工作区 | pending | 只有一个 worktree，HEAD 为 detached；已有两张图片删除不属于本次修改，未恢复、删除或提交；清理候选仍保留 |

## 修正与验证

- GUI PS1 直接在配置的 SSH 目标启动 pvserver，**没有** batch 包装脚本的主机校验。说明与 skill 已明确这一区别，不能用成功回执证明节点正确。
- 手动加载说明改为先下载本机文件并设置 PV_BRIDGE_SCRIPT，使哈希来源与读取文件一致；移除仅 exec 远端源码却沿用旧本机文件哈希的升级示例。
- 当前桥实际 inspect 返回正常，管线源为空、存在 RenderView。未改变背景、加载数据、停止服务或重启用户窗口。
- 本次从超算端核验 GUI 配置的 SSH 目标时，主机密钥校验失败；未关闭主机验证或修改 known_hosts，因此该入口的主机归属保持 pending。
- batch 包装入口经编译节点成功执行 --help；255 个已安装包的文件名及 SHA256 与显式锁完全一致。
- skill validator、两份 Bash 语法、既有 7 项测试、本地 Markdown 链接、规则及兼容软链、Git 差异空白检查均通过。
- 复核保留的迁移导出：1025 点全部有效，9 列数值逐点与迁移前 CSV 相同。本次没有重复启动数据提取，也不扩张为三维正确性证明。

## 保留现场与未覆盖项

- 清理候选仅 `.tools/paraview-batch/skill-migration-check/`：2 个文件，共 107548 字节，属于重复验证 CSV 与元数据；当前仍用于复核证据，未删除。
- `.tools/paraview-batch/env`、包锁、兼容软链、运行数据、既有日志均有现役或诊断用途，不列为清理候选。
- 完整 Windows 启动、当前 GUI 源码身份及其执行节点仍 pending；下次实际启动须核对 Bridge ready、本次回执与节点。
- 本会话的技能列表未展示新增 paraview 项；文件及元数据有效，宿主重新发现后的列表展示仍待确认。
- 远端推送、PR/合并、截图与动画、其他算例及全 workspace 内容审计为 out-of-scope；没有发布或清场声明。
