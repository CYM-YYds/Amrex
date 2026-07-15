# BOX3D README 改写 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 BOX3D 算例 README 改为中文的性能分析说明，并删除与仓库根 README 重复的通用内容。

**Architecture:** 仅改动算例级 README。文档先用一段简洁定位说明三维方腔流和耗时分析目的，再以表格对照 `src/main.cpp` 的三个性能输出，最后链接到两份现有深入资料。

**Tech Stack:** Markdown、Git、AMReX/LBM 源码中的现有输出字段。

## Global Constraints

- 只修改 `projects/3Dcases/Computing_performance_test/BOX3D/README.md`。
- 全文使用中文；命令、函数名、计时字段和文件路径保持原始拼写。
- 不重复根目录 `README.md` 的仓库结构、AMReX 版本和通用编译环境说明。
- 性能字段必须与 `projects/3Dcases/Computing_performance_test/BOX3D/src/main.cpp` 的实际输出一致。
- 本次仅改文档，不构建或运行算例。

---

### Task 1: 改写 BOX3D 算例 README

**Files:**
- Modify: `projects/3Dcases/Computing_performance_test/BOX3D/README.md`
- Verify: `projects/3Dcases/Computing_performance_test/BOX3D/src/main.cpp`
- Verify: `projects/3Dcases/Computing_performance_test/BOX3D/docs/performance_profiling.md`
- Verify: `projects/3Dcases/Computing_performance_test/BOX3D/docs/amr_grid_communication.md`

**Interfaces:**
- Consumes: `main.cpp` 每 1000 步打印的 `perf(s)`、`perf_detail(s)` 与 `perf_count` 行。
- Produces: 一个面向使用者的中文 README，链接到现有的性能分析和 AMR 通信资料。

- [ ] **Step 1: 核对运行时输出字段与资料路径**

运行：

```bash
rg -n "perf\(s\)|perf_detail\(s\)|perf_count" \
  projects/3Dcases/Computing_performance_test/BOX3D/src/main.cpp
test -f projects/3Dcases/Computing_performance_test/BOX3D/docs/performance_profiling.md
test -f projects/3Dcases/Computing_performance_test/BOX3D/docs/amr_grid_communication.md
```

预期：`main.cpp` 包含三行性能输出，且两个文档均存在。

- [ ] **Step 2: 用中文替换 README 内容**

保留以下结构和事实：

```markdown
# BOX3D：三维方腔流耗时分析算例

本算例基于 AMReX 实现三维方腔流的 AMR-LBM 计算。其主要用途是记录并分析时间推进、粗细网格数据传输和重网格等阶段的耗时，为性能归因与优化提供依据。

## 性能统计口径

程序每 1000 步输出一个性能窗口；`perf(s)`、`perf_detail(s)` 与 `perf_count` 分别给出阶段耗时、AMR 传输细分耗时和调用量/近似格点工作量。

## 深入资料

- [性能分析流程与结果](docs/performance_profiling.md)
- [AMR 网格通信与粗细网格传输](docs/amr_grid_communication.md)
```

在“性能统计口径”下补充一个表格，逐项列出 `interp`、`collide`、`stream`、`average`、`comm`、`boundary`、`swap`，以及 `interp_scale`、`interp_fillpatch`、`average_alloc`、`average_copy`、`average_scale`、`average_down` 的含义。说明细分计时会同步 GPU，因此只能用于同一插桩构建内的耗时归因，不用于和未插桩运行比较绝对吞吐。

- [ ] **Step 3: 检查 Markdown 与链接完整性**

运行：

```bash
rg -n '^#|^##|perf\(s\)|perf_detail\(s\)|perf_count|performance_profiling|amr_grid_communication' \
  projects/3Dcases/Computing_performance_test/BOX3D/README.md
git diff --check -- projects/3Dcases/Computing_performance_test/BOX3D/README.md
```

预期：标题层级清晰，三个字段和两个相对链接均可检出，且 `git diff --check` 无输出。

- [ ] **Step 4: 提交文档改写**

```bash
git add projects/3Dcases/Computing_performance_test/BOX3D/README.md
git commit -m "docs: clarify BOX3D cavity performance case"
```

预期：提交仅包含该 README 的改写。
