# 精简 CLAUDE.md

- 目的：压缩 Agent 规则文件篇幅，去除与 README、Skill 重复的描述，规则语义不变。
- 影响范围：根 `CLAUDE.md`（`AGENTS.md` 软链随之生效）。
- 主要改动：合并"开始任务"中的算例范围与配置权威来源说明；结构审查的基线、记录流程
  交由 review-code-structure Skill 描述，仅保留触发边界；合并验证、提交、ai_changes 记录
  及 clean.sh 等分散条目；"现场注意事项"并入"不可省略的约束"。约 2476 字符 → 2026 字符。
- 验证：检查文中链接 `README.md`、`README.md#脚本归属与生命周期`、两个 Skill 路径均存在；
  `AGENTS.md` 仍为指向 `CLAUDE.md` 的软链。
