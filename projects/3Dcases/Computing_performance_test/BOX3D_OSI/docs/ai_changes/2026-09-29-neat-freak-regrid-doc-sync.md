# 重构迁移文档同步

- 目的：使现行交接说明与 `af3b5da` 的 phase 感知重构迁移一致，并明确验收边界。
- 文件：`README.md`、`docs/current_status.md`、`docs/osi_algorithm_and_architecture.md`。
- 主要改动：记录布局不变时保留 phase、布局变化时使用 CPC 标签直接迁移重叠区；标明旧作业属于历史版本，现行实现仍待动态重构逐值验证。
- 验证：对照 `RemakeLevel()`、`RemapOsiLevelDirect()` 和既有变更记录；运行 `git diff --check`。本次仅修改文档，未做数值运行。
