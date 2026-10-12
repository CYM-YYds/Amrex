# 根 CLAUDE.md 与 README.md 职责划分整理

- 目的：消除两份文件对同一事实的重复描述，避免一起过时；README 只讲仓库事实，CLAUDE.md 只讲 Agent 约束。
- 影响范围：根 `README.md`、`CLAUDE.md`（`AGENTS.md` 软链随之生效）、`scripts/sync_projects.py` 生成的算例 README 模板。
- 主要改动：
  - AMReX 版本说明只保留在 README，并按各算例 `config/GNUmakefile` 的 `AMREX_HOME` 实际情况更正：26.06 除 BOX3D 外还被 BOX3D_OSI、`amrex_cumulant_cavity_sphere`、`Cylinder3D`、`Cylinder2D_DF10/DF15` 和部分 `Cylinder2D_BTDF*` 使用；23.09 当前无算例引用。
  - CLAUDE.md 删除版本、编译器、GPU+MPI、测试布局、旧同步脚本路径等事实性条目，改为"版本以 `AMREX_HOME` 为准"的约束；Python 环境条目只保留约束并指向 README 第 5 节。
  - README 目录树补充 `docs/ai_changes/`、`.venv/`、`CLAUDE.md`；说明算例根 `GNUmakefile` 是 include `config/GNUmakefile` 的包装入口；测试布局、GPU+MPI、`clean.sh` 提示移入开发备注。
  - `scripts/sync_projects.py` 模板中的 `tools/sync_projects.py` 改为 `scripts/sync_projects.py`，调用方式改为 `.venv/bin/python`。已生成的算例 README（如 `ChannelFlowtest/*`）未改动。
- 验证：统计全部算例 `config/GNUmakefile` 的 `AMREX_HOME`（26.01 共 25 个、26.06 共 10 个）；核对 README 与 CLAUDE.md 中引用的路径均存在；`AGENTS.md` 仍为指向 `CLAUDE.md` 的软链；`scripts/sync_projects.py` 通过 `py_compile`。
