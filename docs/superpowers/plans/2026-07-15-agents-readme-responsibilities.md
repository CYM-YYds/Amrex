# AGENTS.md 与 README.md 职责拆分实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 精简 `AGENTS.md`，使 `README.md` 成为项目通用信息的唯一来源，并用安全的工作区保护规则取代自动基线提交。

**Architecture:** `README.md` 保持不变并继续说明仓库结构、文件职责和构建运行方法。`AGENTS.md` 首先引导 AI 阅读 README，随后只规定任务范围、工作区保护、验证要求、仓库陷阱和默认上下文。

**Tech Stack:** Markdown、Git

---

### Task 1: 精简并校验 AI 协作规则

**Files:**

- Modify: `AGENTS.md`
- Reference: `README.md`
- Reference: `docs/superpowers/specs/2026-07-15-agents-readme-responsibilities-design.md`

- [ ] **Step 1: 记录修改前状态**

Run:

```bash
git status --short
```

Expected: 无输出；若出现输出，保留已有修改且不自动暂存或提交。

- [ ] **Step 2: 删除 README 已覆盖的项目说明**

从 `AGENTS.md` 删除“仓库结构”“主要代码文件的职责”“构建与运行流程”三个章节。保留“开始前请先阅读”，并明确 `README.md` 是项目结构、文件职责和通用编译运行方法的唯一说明来源。

- [ ] **Step 3: 替换工作区保护规则**

删除自动运行 `git add -A` 和创建 `before codex` 提交的三条规则，替换为以下完整规则：

```markdown
- 编辑前先运行 `git status --short`，确认工作区中是否已有修改。
- 工作区中的已有修改属于用户；不得自动暂存、提交、覆盖或回退与当前任务无关的改动。
- 只有用户明确要求时才创建提交；提交时只包含用户授权范围内的文件。
```

- [ ] **Step 4: 保留任务相关检查规则**

确认 `AGENTS.md` 仍要求：以具体算例为边界；按顺序阅读目标文件；修改物理或 AMR 行为前阅读 `config/inputs`；编译任务检查根脚本和算例 `GNUmakefile`；运行任务检查算例 `submit.sh`；根据风险执行构建或模拟验证。

- [ ] **Step 5: 检查重复内容和 Markdown**

Run:

```bash
rg -n "## 仓库结构|## 主要代码文件的职责|## 构建与运行流程|git add -A|before codex" AGENTS.md
git diff --check
git diff --stat
```

Expected: 第一条命令无输出；`git diff --check` 无输出且退出码为 0；`git diff --stat` 只显示 `AGENTS.md` 被修改。

- [ ] **Step 6: 人工核对保留项**

Run:

```bash
rg -n "README.md|git status --short|config/inputs|config/GNUmakefile|scripts/submit.sh|scripts/clean.sh|amrex-26.01|GPU \+ MPI" AGENTS.md
```

Expected: 所有关键词均有匹配，证明项目事实入口、工作区保护、任务检查、仓库陷阱和默认上下文仍然存在。
