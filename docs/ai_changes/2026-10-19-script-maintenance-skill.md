# 脚本生命周期管理规则与 Skill

- 目的：约束日常辅助脚本增长，并为周期性审查、功能沉淀与清理提供独立流程，保留科研复现能力。
- 影响范围：根 `CLAUDE.md`（经 `AGENTS.md` 软链生效）、根 `README.md`、`.agents/skills/script-maintenance/SKILL.md`；未迁移、重构或删除现有脚本，未修改求解器或运行配置。
- 主要改动：
  - 日常规则只约束新增、参数复用、归属与入口说明，周期性维护流程集中在 Skill。
  - Skill 默认只读审查，覆盖临时目录及旧有平铺布局，按 KEEP/PROMOTE/MERGE/ARCHIVE/DELETE/REVIEW 分类；具体方案确认后才实施，并检查调用关系、等价性和复现链。
  - 根目录承接仓库管理及实际跨算例公共机制；算例专用运行工具、后处理、回归验证各留在对应目录；Skill 专用执行机制继续由 Skill 管理。一次性脚本按归属放入 `scripts/temporary/`，不强制迁移已有长期入口到 `persistent/`。
  - 第一版只增加 `SKILL.md`，不引入扫描脚本、空目录或自动清理任务。审查不提交 Git，实施时遵循用户指令与仓库现有变更记录、提交规则。
- 验证：使用根 `.venv/bin/python` 运行 skill-creator 的 `quick_validate.py`，结果为 `Skill is valid!`；检查三份规则/说明中的 8 个本地 Markdown 链接及显式锚点，均有效；确认 `AGENTS.md -> CLAUDE.md` 和 Skill 目录仅含 `SKILL.md`；`git diff --check` 通过。另核对只读审查、已确认方案实施、证据不足及旧布局兼容等流程边界，未执行实际脚本清理或数值验证。
