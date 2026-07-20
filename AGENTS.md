# 本仓库的 AI 协作指南

本仓库包含基于 AMReX 的 LBM + AMR 数值模拟算例。主要开发模式以算例为中心：大多数工作应在 `projects/` 下的具体算例目录中进行，而不是在仓库根目录进行。

## 开始前请先阅读

开始一项新任务时，请按以下顺序阅读文件：

1. `README.md`
2. `projects/` 下的目标算例目录
3. `<case>/src/main.cpp`
4. `<case>/src/AmrCoreLBM.H` 和 `<case>/src/AmrCoreLBM.cpp`
5. `<case>/config/inputs`
6. `<case>/config/GNUmakefile`
7. 如果任务涉及构建行为，再阅读 `scripts/compile.sh`

对于圆柱流相关工作，推荐将 `projects/Cylindertest/Cylinder2D_IDFtest/` 作为默认算例。

## 仓库结构

- `amrex-26.01/`：当前主要使用的 AMReX 源码树
- `amrex-23.09/`：为对比或兼容性而保留的旧版 AMReX
- `amrex-26.06/`：用于上游版本对照，不是现有算例的默认构建版本
- `projects/2Dcases/`、`projects/3Dcases/`、`projects/Cylindertest/`：数值模拟算例
- `projects/2Dshared/`、`projects/3Dshared/`：共享封装或模板
- `scripts/`：仓库级编译、同步和清理工具
- `后处理脚本/`：后处理脚本和说明

典型算例的目录结构：

- `src/`：主要源代码
- `config/`：`GNUmakefile`、`Make.package`、`inputs`，通常还包括 `compile_commands.json`
- `scripts/`：算例本地的编译、清理和提交封装脚本
- `data/`、`logs/`、`tmp_build_dir/`：生成的输出和构建产物

## 主要代码文件的职责

- `src/main.cpp`：AMReX 初始化、参数读取、时间推进和输出触发
- `src/AmrCoreLBM.*`：AMR/LBM 驱动逻辑、数据管理、时间推进、网格加密和诊断
- `src/Kernels.H`：性能敏感的计算核，以及边界、碰撞和迁移辅助函数
- `src/D2Q9.H` 或 `src/D3Q19.H`：格子常量和离散速度模板定义
- `src/LagrangeParticleContainer.*`：粒子或浸没边界类型的耦合（如果算例中存在）

## 构建与运行流程

优先在目标算例目录内操作。

编译：

```bash
./scripts/compile.sh
```

在集群上提交：

```bash
dsub -s ./scripts/submit.sh
```

一步完成编译和提交：

```bash
./scripts/compile.sh --submit
```

重要说明：

- 仓库根目录的 `scripts/compile.sh` 是实际的构建入口；各算例本地的编译脚本只是封装。
- 构建脚本依赖 HPC 环境，并且可能跳转到 `whshare-agent-1`。
- 提交脚本依赖 DSUB、MPI、CUDA 模块以及集群特定的主机环境配置。
- `config/GNUmakefile` 是每个算例构建设置的主要来源，其中包括 `DIM`、`USE_MPI`、`USE_CUDA` 和 `AMREX_HOME` 等配置。

## AI 智能体工作约定

- 从一个具体算例开始，除非任务明确涉及多个算例，否则应始终限制在该算例的范围内。
- 将已纳入版本控制的输出视为本仓库的正常内容：可执行文件、`*-out.log`、`.dat`、`logs/` 和生成的数据可能已经存在。
- 不要假设本仓库具有常规的单元测试布局；验证方式通常是目标算例成功构建，或数值模拟行为符合预期。
- 修改任何代码前，必须先运行 `git add -A` 和 `git commit -m "before codex"`，创建供审查使用的基线提交。
- 如果 `git commit -m "before codex"` 提示没有内容可提交，则无需创建提交并可继续工作。
- 如果基线提交因其他原因失败，必须在编辑文件前停止并报告失败情况。
- 在修改物理模型、时间推进、输出频率或 AMR 行为前，优先阅读 `config/inputs`。
- 如果任务涉及编译行为，请检查 `scripts/compile.sh` 和目标算例的 `config/GNUmakefile`。
- 如果任务涉及运行和提交行为，请检查目标算例的 `scripts/submit.sh`。

## 本仓库当前的特别注意事项

- 一些旧文档提到 `tools/sync_projects.py`，但该脚本目前实际位于 `scripts/sync_projects.py`。
- 本仓库并非只有一个入口点的单一程序库；各个算例可能独立演化并产生差异。
- 一些以前编写的 AI 指令可能提到 `projects/shared/lbm-core/` 或 `tools/` 等路径；依赖这些路径前必须先核实它们当前是否存在。
- 仓库根目录的 `scripts/clean.sh` 影响范围较广，可能具有破坏性；运行前必须仔细检查。

## 后续任务默认采用的上下文

除非用户另有说明，否则默认：

- 主要 AMReX 版本为 `amrex-26.01`
- 大多数活跃工作位于 `projects/Cylindertest/` 或 `projects/` 下的具体算例目录中
- 代表性生产算例预期使用 GPU + MPI 构建
- `config/inputs` 和 `src/main.cpp` 是理解算例运行行为最快的切入点
