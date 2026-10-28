# LBM-AMR on AMReX

本仓库用于维护基于 AMReX 的 LBM + AMR 算例开发与实验代码，包含 2D/3D 多类流动场与粒子耦合场景。

各算例实际使用的 AMReX 版本以其 `config/GNUmakefile` 中的 `AMREX_HOME` 为准，
目前 `amrex-26.01` 与 `amrex-26.06` 并存（见 2.1 节）。

## 1. 项目架构概览

仓库采用“AMReX 源码 + 多算例工程 + 公共脚本/配置”的组织方式：

```text
Amrex/
├── amrex-23.09/               # AMReX 23.09 源码
├── amrex-26.01/               # AMReX 26.01 源码（多数算例使用）
├── amrex-26.06/               # AMReX 26.06 源码（BOX3D 系列与部分圆柱流算例使用）
├── projects/                  # 全部算例工程
│   ├── 2Dcases/               # 2D 具体算例
│   ├── 3Dcases/               # 3D 具体算例
│   ├── 2Dshared/              # 2D 共享组件/模板
│   ├── 3Dshared/              # 3D 共享组件/模板
│   └── Cylindertest/          # 圆柱流系列算例
├── research/                  # 论文、学习笔记与参考实现（不参与构建）
├── scripts/                   # 仓库级编译、同步、提交、审计脚本
├── docs/ai_changes/           # 跨算例工具、仓库规则与根目录文档的变更记录
├── 后处理脚本/               # 算例可视化脚本与 ParaView 连接兼容入口
├── .agents/skills/            # 项目工作流，ParaView 自动化核心实现位于其中
├── .venv/                     # 全仓库共用的 Python 后处理环境
├── CLAUDE.md                  # AI 协作规则（AGENTS.md 为其软链）
└── README.md
```

## 2. 各目录作用说明

### 2.1 AMReX 版本目录

算例使用哪个版本只看其 `config/GNUmakefile` 中的 `AMREX_HOME`，本节仅作概览：

- `amrex-26.01/`: 多数算例（2D/3D 通用算例、大部分圆柱流算例）使用。
- `amrex-26.06/`: `Computing_performance_test/` 下的 BOX3D、BOX3D_OSI、
  `amrex_cumulant_cavity_sphere`，以及 `Cylindertest/` 下的 `Cylinder3D`、
  `Cylinder2D_DF10`、`Cylinder2D_DF15` 和部分 `Cylinder2D_BTDF*` 使用。
  BOX3D 系列需要 C++20 与 GCC 11+。
- `amrex-23.09/`: 历史对照版本，便于迁移排查与行为对比；当前没有算例配置引用它。

### 2.2 算例目录

- `projects/2Dcases/`: 2D 场景算例集合。
- `projects/3Dcases/`: 3D 场景算例集合。
- `projects/Cylindertest/`: 圆柱相关的 2D/3D 流动算例与参数扫描。
- `projects/2Dshared/` 与 `projects/3Dshared/`: 共享代码或模板，减少重复维护。

### 2.3 支撑目录

- `research/`: 论文原件、论文解析、学习笔记和参考代码；详细分类见 [`research/README.md`](research/README.md)。这些资料不属于任何算例的构建输入。
- `scripts/`: 仓库级工具脚本，如批量编译、清理、同步工程。
- `后处理脚本/`: 数据处理与可视化脚本（例如涡量、Q-criterion、流线展示）。

<a id="脚本归属与生命周期"></a>

### 2.4 脚本归属与生命周期

脚本按服务对象归属，调用时所在目录不决定归属。长期与临时表示维护状态，
不要求把已有目录统一改成 `persistent/temporary/`。

| 服务对象与职责 | 长期入口或实现的位置 | 示例 |
|---|---|---|
| 仓库管理、跨算例且语义兼容的公共机制 | 根 `scripts/`，内部共用实现可在 `scripts/lib/` | 工程同步、公共编译流程、代码审计 |
| 某个算例的构建、运行或实验流程 | `<case>/scripts/` | 算例编译/提交入口、OSI 专用运行工具 |
| 某个算例的数据提取、分析与绘图 | `<case>/data_post_processing/` | 中心线提取、收敛分析、参考数据对比 |
| 某个算例的回归验证 | `<case>/tests/` | 阶段检查、数值回归驱动 |
| Skill 专用的执行机制 | `.agents/skills/<skill>/scripts/` | ParaView 桥接与批处理实现 |

一次性分析、调试与实验脚本默认放在所属范围的 `scripts/temporary/`：
仓库级任务使用根目录下的该路径，算例任务使用 `<case>/scripts/temporary/`。
目录按需创建；数据和图表仍写入所属实验的结果目录，不因脚本临时而自动清理结果。
已随历史结果保留的复现脚本按历史证据处理，不因所在位置判定需要迁移。

用户直接使用的工具默认长期维护；AI 为当前任务生成的一次性检查脚本默认临时，
在文件头注明用途与任务结束后可删除。任务完成后可清理这类已明确标记、无后续依赖的脚本；
由 AI 调用的正式工具依赖、回归驱动与关键结果唯一生成脚本仍需保留。
旧脚本不因作者是 AI、缺少引用或创建较早就被视为可删除。

现有 `scripts/`、`data_post_processing/` 中的长期入口保持兼容；本约定不触发批量搬迁。
新增长期入口时，在所属范围的 README 中维护职责、输入输出与调用示例；只有实际采用
`scripts/persistent/` 布局时，才在其 README 维护索引，不重复登记两套说明。

算例专用工具先留在算例内。存在实际跨算例需求，且输入输出、默认行为及维护职责兼容时，
再考虑提取公共实现；仅名称或代码相似不构成上移依据。跨算例对比工具若仍绑定某一实验，
可留在主责算例并显式接收其他算例路径。根目录工具通过显式路径或参数选择算例，
避免内置某个算例的作业号、参考数据或物理判据。已有编译/提交入口、测试入口与 Skill
执行脚本各有职责，保留有实际作用的委托关系，不为统一目录新增转发层。

周期性维护使用 [script-maintenance](.agents/skills/script-maintenance/SKILL.md)：
只要求审查时只读；要求清理时优先按仓库授权清理已完成的 AI 临时脚本。
长期工具的整合、迁移或其他删除先形成具体方案，用户确认后实施。
验证随操作选择：独立临时脚本删除检查引用与复现用途，改变计算逻辑才做针对性数值回归，
不默认重放历史实验。只提供 Skill 指令，不增加自动扫描程序或自动清理任务。

## 3. 具体算例文件架构说明

一个典型算例目录（如 `projects/Cylindertest/Cylinder3D/` 或 `projects/Cylindertest/Cylinder2D_IDFtest/`）通常包含以下结构：

```text
<case>/
├── GNUmakefile                # 自动生成的包装入口，设置 VPATH 后 include config/GNUmakefile
├── config/                    # 生成或整理后的构建配置
│   ├── GNUmakefile            # 实际构建配置（AMREX_HOME、COMP、USE_CUDA 等）
│   ├── Make.package
│   ├── inputs
│   └── compile_commands.json
├── src/                       # 算例核心源码
│   ├── main.cpp               # 程序入口
│   ├── AmrCoreLBM.H/.cpp      # AMR + LBM 驱动主逻辑
│   ├── Kernels.H              # GPU/并行计算核函数
│   ├── D3Q19.H 或 D2Q9.H      # 格子模型定义（3D/2D）
│   └── LagrangeParticleContainer.*  # 粒子或浸没边界耦合
├── scripts/                   # 算例级脚本（编译/提交/清理）
├── data/                      # 数值结果输出目录
├── logs/                      # 编译和运行日志
├── tmp_build_dir/             # 中间编译产物
├── .clangd/.clang-tidy/...    # 算例级开发与静态分析配置
└── main*.ex                   # 编译生成可执行文件
```

### 3.1 `src/` 内核心文件职责

- `main.cpp`: 初始化 AMReX、读取输入参数、驱动时间推进。
- `AmrCoreLBM.H/.cpp`: 负责多层 AMR 数据管理、推进流程与耦合调度。
- `Kernels.H`: 包含碰撞、推进、宏观量计算、边界处理等关键计算核。
- `D3Q19.H` 或 `D2Q9.H`: LBM 离散速度集与权重等常量定义。
- `LagrangeParticleContainer.*`: 粒子/浸没边界耦合数据结构与算法。

### 3.2 `config/` 与 `inputs`

- `inputs`: 运行参数（网格、时间步、输出频率、LBM 参数、AMR 参数）。
- `compile_commands.json`: clangd/IDE 精准语义分析与跳转依据。
- `Make.package`: 汇总源文件、头文件路径与构建依赖。

### 3.3 `data/`、`logs/`、`tmp_build_dir/`

- `data/`: PlotFile 与统计数据输出。
- `logs/`: 编译/运行日志，便于回溯配置与排错。
- `tmp_build_dir/`: 目标文件与依赖文件，通常无需手工修改。

## 4. 算例编译与运行（在算例根目录执行）

以下命令均在具体算例根目录执行，例如 `projects/Cylindertest/Cylinder2D_IDFtest/`。

```bash
# 1) 进入算例根目录
cd projects/Cylindertest/Cylinder2D_IDFtest

# 2) 编译（使用算例脚本）
./scripts/compile.sh

# 3) 提交运行（使用算例提交脚本）
dsub -s ./scripts/submit.sh
```

说明：

1. 编译与运行都应在算例根目录内执行，避免路径与配置错位。
2. 编译完成后可执行文件会出现在算例目录中（如 `main2d...ex` 或 `main3d...ex`）。
3. 公共编译入口可通过 `AMREX_BUILD_PRECISION=DOUBLE/FLOAT` 显式向本地/远程 make 传递精度；
   未设置时保持各算例构建配置的默认值。BOX3D_OSI 的用户入口使用 `PRECISION`，见其 README。
4. 运行日志通常可在算例目录的 `*.log` 或 `logs/` 下查看。
5. 结果数据通常输出到 `data/`。

调用公共 `scripts/submit.sh` 的入口会显式加载 MPI、CUDA 和 GCC，并向 MPI rank
传递计算环境。rank 的 GPU 绑定使用不读取个人启动文件的 Bash，且清除 `BASH_ENV`；
计算所需设置应写入提交脚本，不应依赖 `~/.bash_profile` 或 `~/.bashrc`。
独立维护的历史实验脚本以各自实现为准。

## 5. Python 后处理环境

仓库根 `.venv/` 是所有算例共用的 Python 虚拟环境，供后处理、数据分析和绘图脚本
使用。算例目录不需要各自维护 `.venv`，也不应依赖系统 Python 的包集合；缺依赖时只在
该环境中补齐。

ParaView 独立批处理使用应用自带的 `pvbatch` 解释器读取数据；后续 CSV 分析和绘图仍使用
本节的 `.venv`。使用方式和核心脚本统一维护在 [ParaView skill](.agents/skills/paraview/SKILL.md)，
可用 `$paraview` 调用，包含当前 GUI 桥接与独立批处理。批处理入口自动跳转编译节点
`whshare-agent-1`，不在登录节点运行 ParaView。

ParaView 的动作和字段统一声明在 `paraview_protocol.py`；源码将桥接运行层
（`paraview_bridge_runtime.py`）与操作层分开，操作层再分为数据管线
（`paraview_operations.py`）和出图排版（`paraview_publication.py`）；`build_paraview_client.py`
生成 Windows 单文件客户端和独立的 Gradient 修复脚本，
具体维护与打包命令见 [实现与维护](.agents/skills/paraview/references/maintenance.md)。

从仓库根调用：

```bash
.venv/bin/python <case>/data_post_processing/script.py
```

从任意算例目录调用时，先定位仓库根，或使用相对路径调用其 `.venv/bin/python`。

## 6. 开发备注

- 本仓库没有统一的单元测试布局，验证通常是目标算例构建成功或数值行为符合预期。
- 代表性生产算例预期采用 GPU + MPI 运行。
- `scripts/clean.sh` 影响范围广，运行前先检查其清理目标。

- 本仓库包含多个算例并行演化，建议每个算例目录维护独立的 `.clangd` 与 `config/compile_commands.json`，减少跨算例语义干扰。
- 使用根目录 `learnamerx.code-workspace` 切换多个算例时，`clangd.arguments` 中的 `--query-driver` 路径必须以逗号分隔。生成数据库后应确认编译器、C++ 标准和 `AMREX_HOME` 与该算例一致，再重启 clangd。
- 算例目录中的 `.clangd` 只作用于该目录及其子目录；`Index.Background` 控制后台索引，不会禁止 clangd 诊断用户主动打开的其他文件。
