# 阶段感知的 AMR 诊断技能

- 目的：把 AMR-LBM A-B/OSI 一致性检查范围绑定到每个计算阶段的实际读写集合。
- 文件：`.agents/skills/stage-aware-amr-diagnostics/SKILL.md`。
- 内容：区分首个差异扫描与锁定步后的精细诊断，分别规定重构、插值、碰撞、通信、迁移、边界和平均阶段的 valid/covered/interface/ghost 范围及结论门槛。
- 验证：使用 Python 3 检查 YAML frontmatter、技能名称、描述和主要章节；技能文件 84 行，无未完成模板占位符。
