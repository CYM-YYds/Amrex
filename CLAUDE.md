# 本仓库的 AI 协作指南

用户可见的仓库结构、典型算例布局、构建命令和 Python 后处理用法以根
[`README.md`](README.md) 为准。本文件只定义 Agent 的执行约束。

## 开始任务

1. 阅读根 `README.md`，再定位目标算例。
2. 根据任务范围阅读目标算例：涉及推进、AMR 或通信时依次阅读
   `<case>/src/main.cpp`、`<case>/src/AmrCoreLBM.H/.cpp`、`<case>/config/inputs`
   和 `<case>/config/GNUmakefile`；文档、后处理或脚本任务只阅读直接相关文件。
   涉及构建或提交时再读 `<case>/scripts/compile.sh` 或 `scripts/compile.sh`、
   `<case>/scripts/submit.sh`。
3. 除非任务明确涉及多个算例，否则将改动限制在一个具体算例内。圆柱流默认算例是
   `projects/Cylindertest/Cylinder2D_IDFtest/`。

## 不可省略的约束

- `CLAUDE.md` 是规则真身；根 `AGENTS.md` 必须保持为指向它的软链。
- 修改前必须检查 `git status` 和相关 `git diff`，确认现有修改的归属，不得自动提交用户已有的无关修改。
- 完成代码修改并通过必要验证后，必须提交本次修改；提交信息必须简要说明实际改动内容。
- 对代码、配置、构建方式、物理模型、AMR、通信或实验流程的 **实质性变更** ，在验证完成后，按一个连贯的变更集在 `docs/ai_changes/` 新增一份简短记录，并与变更一起提交。记录包括修改目的、影响范围、主要改动和验证结果。临时诊断、重复实验、纯格式或机械性修改无需单独记录。
- 算例内的变更记录放在对应 `<case>/docs/ai_changes/`；跨算例工具、仓库规则或根目录文档的记录放在根 `docs/ai_changes/`，不得混用两处目录。
- 验证方式按改动类型选择：文档至少检查链接和路径，脚本至少运行 shell 语法检查，构建配置至少完成目标算例编译，数值路径还需运行与改动阶段对应的逐值或回归检查。
- 修改代码时添加的注释必须是中文形式。
- 已跟踪的可执行文件、日志、`.dat`、`logs/` 和生成数据均可能是有效仓库内容，不能按常规构建残留假定删除。
- 修改物理模型、时间推进、输出频率或 AMR 行为前先读 `config/inputs`；构建和运行行为
  分别以对应编译/提交脚本及 `config/GNUmakefile` 为准。
- 运行所有算例的 Python 后处理、分析或绘图脚本时，使用仓库根 `.venv/bin/python`。
  它是全仓库共用环境，不应改用系统 Python 或假定算例有独立 `.venv`；缺依赖时只在该环境中补齐。图表必须标注输入数据、步数、归一化方式和参考数据来源。

## 现场注意事项

- 本仓库没有统一单元测试布局；验证通常是目标算例构建成功或数值行为符合预期。
- `scripts/sync_projects.py` 是现役同步脚本；引用旧的 `tools/sync_projects.py` 前先核实。
- `scripts/clean.sh` 影响范围广，运行前必须检查其目标。
- 默认主 AMReX 为 `amrex-26.01`；BOX3D 使用 `amrex-26.06`、C++20 和 GCC 11+。
  代表性生产算例预期采用 GPU + MPI。
