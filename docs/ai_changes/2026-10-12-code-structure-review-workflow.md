# 代码复用约束与只读结构审查 skill

- 目的：减少增量开发中的重复职责、重复状态和未退出的旧实现，同时避免为 DRY 过度抽象。
- 范围：[CLAUDE.md](../../CLAUDE.md) 与新增的
  [review-code-structure/SKILL.md](../../.agents/skills/review-code-structure/SKILL.md)；
  根 AGENTS.md 保持指向 CLAUDE.md 的软链。
- 常驻规则：修改前分层搜索并说明复用选择；明确状态来源、生命周期与旧路径去留；
  无关历史重复只记录；新增模块、持久状态、替代路径或跨文件职责调整时执行完整只读审查，
  小改动简短自查；修复后验证再提交。
- Skill：明确提交或工作区审查基线，覆盖相关未跟踪文件；从 diff 追踪调用与数据所有权，
  对照已有机制；要求 findings 提供双方位置、语义证据、维护后果、最小建议和验证要求；
  区分待确认候选与有意保留的独立实现。审查不修改源码、不自动修复或提交。
- 调用：`$review-code-structure 审查当前工作区修改，只给 findings`，或指定基线与目标提交。
- 验证：skill-creator 的 `quick_validate.py` 通过；本地链接检查、AGENTS.md 软链及
  解析内容一致性检查、`git diff --check` 通过。人工检查了基线选择、审查/修复边界及
  数值对照保留规则的一致性；未进行独立模型行为评测，格式校验不代表审查效果已获证明。
- 环境：校验器缺少 PyYAML，使用现有 uv 在仓库 `.venv` 补齐 PyYAML 6.0.3；
  没有增加 skill 脚本或修改生产构建、算例源码与运行配置。
