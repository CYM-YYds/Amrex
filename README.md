# LBM-AMR on AMReX

本仓库用于维护基于 AMReX 的 LBM + AMR 算例开发与实验代码，包含 2D/3D 多类流动场与粒子耦合场景。

当前主要使用的 AMReX 版本为 `amrex-26.01`，同时保留 `amrex-23.09` 以便对比与兼容测试。

## 1. 项目架构概览

仓库采用“AMReX 源码 + 多算例工程 + 公共脚本/配置”的组织方式：

```text
Amrex/
├── amrex-23.09/               # AMReX 23.09 源码
├── amrex-26.01/               # AMReX 26.01 源码（当前主用）
├── projects/                  # 全部算例工程
│   ├── 2Dcases/               # 2D 具体算例
│   ├── 3Dcases/               # 3D 具体算例
│   ├── 2Dshared/              # 2D 共享组件/模板
│   ├── 3Dshared/              # 3D 共享组件/模板
│   └── Cylindertest/          # 圆柱流系列算例
├── scripts/                   # 仓库级编译、同步、提交脚本
├── 后处理脚本/               # 可视化与后处理脚本（Python/ParaView）
└── README.md
```

## 2. 各目录作用说明

### 2.1 AMReX 版本目录

- `amrex-26.01/`: 当前主版本，算例默认优先对齐该版本头文件与接口。
- `amrex-23.09/`: 历史版本/对照版本，便于迁移排查与行为对比。

### 2.2 算例目录

- `projects/2Dcases/`: 2D 场景算例集合。
- `projects/3Dcases/`: 3D 场景算例集合。
- `projects/Cylindertest/`: 圆柱相关的 2D/3D 流动算例与参数扫描。
- `projects/2Dshared/` 与 `projects/3Dshared/`: 共享代码或模板，减少重复维护。

### 2.3 支撑目录

- `scripts/`: 仓库级工具脚本，如批量编译、清理、同步工程。
- `后处理脚本/`: 数据处理与可视化脚本（例如涡量、Q-criterion、流线展示）。

## 3. 具体算例文件架构说明

一个典型算例目录（如 `projects/Cylindertest/Cylinder3D/` 或 `projects/Cylindertest/Cylinder2D_IDF/`）通常包含以下结构：

```text
<case>/
├── GNUmakefile                # 算例构建入口（GNUmake）
├── config/                    # 生成或整理后的构建配置
│   ├── GNUmakefile
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

以下命令均在具体算例根目录执行，例如 `projects/Cylindertest/Cylinder2D_IDF/`。

```bash
# 1) 进入算例根目录
cd projects/Cylindertest/Cylinder2D_IDF

# 2) 编译（使用算例脚本）
./scripts/compile.sh

# 3) 提交运行（使用算例提交脚本）
dsub -s ./scripts/submit.sh
```

说明：

1. 编译与运行都应在算例根目录内执行，避免路径与配置错位。
2. 编译完成后可执行文件会出现在算例目录中（如 `main2d...ex` 或 `main3d...ex`）。
3. 运行日志通常可在算例目录的 `*.log` 或 `logs/` 下查看。
4. 结果数据通常输出到 `data/`。

## 5. 备注

- 本仓库包含多个算例并行演化，建议每个算例目录维护独立的 `.clangd` 与 `config/compile_commands.json`，减少跨算例语义干扰。
