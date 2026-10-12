# 结构审查改为定期增量审查

- 目的：开发任务不再承担提交前结构审查，降低小改动的流程负担；改由用户定期另开任务独立审查，保证审查有明确、可追溯的基线。
- 范围：[CLAUDE.md](../../CLAUDE.md) "修改代码与结构审查"一节；
  [review-code-structure/SKILL.md](../../.agents/skills/review-code-structure/SKILL.md)。
- 主要改动：
  - CLAUDE.md：删除提交前完整审查/简短自查要求；"说明复用选择与理由"只在新增函数、类、模块或替代路径时要求；新增定期审查条目，指向 skill 和审查记录。
  - Skill：默认模式改为增量审查，基线取自 `<case>/docs/structure_review_log.md` 最后一条记录的目标提交，审查 `B..HEAD` 的累计改动，但重复判断与算例当前全部代码比较；无记录、历史被改写或用户要求时做整体审查并建立基线；上次"有意保留"不重复报告，"待确认"重新核对。
  - Skill 新增“静态审计工具”一节：增量与整体审查都运行 `scripts/code_audit.sh`，固定默认阈值以保证前后可比；`<case>/docs/code_audit_allow.txt` 作为有意保留未调用函数的唯一来源，用 `-a` 传入；记录趋势，增量审查只核对落在改动文件内的条目；工具缺失时不自动安装。脚本本身不改动。
  - Skill 的写操作只限于追加审查记录和白名单，并只提交这两个文件；不改源码、不重构。
- 验证：`git diff --check` 通过；skill 与 CLAUDE.md 中的链接、`scripts/code_audit.sh` 路径存在；`AGENTS.md` 仍为软链。用临时白名单对 `BOX3D_OSI/src` 运行 `code_audit.sh -a`（约 86 秒），带行尾 `#` 注释的条目被正确排除。尚未对实际算例跑过一次完整的增量或整体审查，流程效果未经验证。
