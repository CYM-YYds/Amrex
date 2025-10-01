# LBM-AMR 模拟项目：静止/运动球体绕流

## 项目概述

本项目是一个基于 C++ 和 AMReX 框架的高性能计算模拟程序，旨在通过**格子玻尔兹曼方法 (Lattice Boltzmann Method, LBM)** 结合**自适应网格细化 (Adaptive Mesh Refinement, AMR)**技术，模拟流体绕过单个或多个球体的复杂流动现象。

代码支持多种 LBM 碰撞模型（如 BGK、累积量模型），并利用 AMR 技术在固体边界和流场中高梯度区域（如尾涡）自动加密网格，从而在保证计算精度的同时，极大地提高了计算效率。此外，项目还集成了**浸入边界法 (Immersed Boundary Method, IBM)** 来处理流体与运动固体之间的相互作用。

---

## 项目文件结构

以下是本项目核心文件的结构及其作用：

### 1. `main.cpp`
*   **作用**: **程序主入口和顶层控制器**。
*   **职责**:
    *   初始化 AMReX 环境和 MPI。
    *   从 `inputs` 文件中读取模拟参数（如最大步数、网格大小、加密层级等）。
    *   创建并初始化核心的 `AmrCoreLBM` 对象。
    *   管理主时间循环 (`for` 循环)，按步推进模拟。
    *   在主循环中调用高级函数，如 `RohdeCycle` 或 `JaberCycle` 来执行一个时间步的演化。
    *   控制数据的输出（如 `WriteVelocityFile`）和网格的重构 (`RefineMesh`)。

### 2. `AmrCoreLBM.H` / `AmrCoreLBM.cpp`
*   **作用**: **AMR 框架的实现者和 LBM 流程的组织者**。
*   **职责**:
    *   定义 `AmrCoreLBM` 类，该类继承自 AMReX 的 `amrex::AmrCore`。
    *   管理多层级的 AMR 网格结构 (`BoxArray`, `DistributionMapping`)。
    *   创建和管理存储流场数据的多层级数据容器 `MultiFab` (如 `fold`, `fnew`, `rho`, `u` 等)。
    *   实现高层级的物理操作，如 `Collide()`, `Stream()`, `Boundary()` 等。这些函数通常会在内部调用 `Kernels.H` 中定义的底层计算核。
    *   实现多层级网格间的数据通信，如 `FillGhostLevel()` (粗到细) 和 `AverageDownGhostLevel()` (细到粗)。
    *   管理粒子的初始化、运动以及流固耦合力的计算。

### 3. `Kernels.H`
*   **作用**: **计算核心，包含所有底层的、计算密集型的核函数 (Kernels)**。
*   **职责**:
    *   定义各种 LBM 碰撞模型（BGK, Cumulant）的具体实现。这些函数直接在单个网格点上操作。
    *   定义 LBM 的迁移 (`stream`)、宏观量计算 (`compute_macro`)、涡量计算 (`compute_vorticity`) 等底层算法。
    *   定义 AMR 的**加密标记函数** (`state_error_...`)，根据物理标准决定哪里需要加密。
    *   定义浸入边界法 (IBM) 的核心函数，包括**力的插值与分布** (`force_interp_extrap`) 和**离散 delta 函数** (`delta3p`)。
    *   所有函数都被优化以在 CPU 和 GPU 上高效运行 (`AMREX_GPU_DEVICE`, `AMREX_FORCE_INLINE`)。

### 4. `D3Q19.H`
*   **作用**: **LBM 模型的配置文件**。
*   **职责**:
    *   定义了所使用的格子模型（这里是 D3Q19，即三维十九速模型）的参数。
    *   包含离散速度向量 `e[Q]` 和对应的权重 `w[Q]`，这些是 LBM 平衡态分布函数计算的基础。

### 5. `LagrangeParticleContainer.H`
*   **作用**: **拉格朗日颗粒的核心实现**。
*   **职责**:
    *   定义 `LagrangeParticleContainer` 类，用于描述一个物理颗粒（球体）。
    *   管理构成颗粒表面的离散拉格朗日点，每个点都携带局部坐标、受力、力矩等信息。
    *   存储颗粒的宏观属性，如中心位置、速度、角速度、总受力等。
    *   实现颗粒的运动更新 (`MoveParticle`) 和碰撞逻辑 (`CollideParticle`, `CollideWall`)。
    *   是实现浸入边界法（IBM）的关键，与 `Kernels.H` 中的力计算函数紧密协作。

### 6. `AuxiliaryPointContainer.H`
*   **作用**: **辅助点容器**。
*   **职责**:
    *   定义 `AuxiliaryPointContainer` 类，管理一组用于特殊目的的"辅助点"。
    *   这些点可用于在流场中特定位置进行数据插值和探测 (`InterpCp`)，或者施加特定的边界条件。

### 7. `InitParticles.H`
*   **作用**: **颗粒初始位置生成器**。
*   **职责**:
    *   提供 `generateSpheres` 函数，用于生成单个或多个颗粒的初始中心坐标。
    *   支持随机分布和规则阵列两种生成方式。
    *   在模拟开始前被调用，为 `LagrangeParticleContainer` 的创建提供位置数据。

### 8. `inputs` (文件)
*   **作用**: **模拟的运行时参数配置文件**。
*   **职责**:
    *   控制模拟的各种参数，无需重新编译代码。
    *   例如：`amr.n_cell`, `max_step`, `stop_time`, `amr.plot_int`, `amr.regrid_int`, `lbm.tau` 等。

---

## 文件关系与依赖结构

```
main.cpp (程序入口)
├── inputs (配置文件 - 运行时参数来源)
├── AmrCoreLBM.H (核心类定义)
│   ├── AmrCoreLBM.cpp (核心类实现)
│   │   ├── D3Q19.H (LBM模型参数定义)
│   │   └── Kernels.H (计算核函数)
│   ├── LagrangeParticleContainer.H (粒子容器类定义)
│   │   ├── LagrangeParticleContainer.cpp (粒子容器实现)
│   │   │   ├── D3Q19.H (LBM模型参数)
│   │   │   └── Kernels.H (GPU计算核心)
│   │   └── InitParticles.H (粒子初始化函数)
│   ├── AuxiliaryPointContainer.H (辅助点容器类定义)
│   │   └── AuxiliaryPointContainer.cpp (辅助点容器实现)
│   │       └── D3Q19.H (LBM模型参数)
│   ├── Kernels.H (GPU/CPU计算核函数定义)
│   │   └── D3Q19.H (LBM D3Q19模型常量和参数)
│   └── D3Q19.H (D3Q19 LBM模型核心定义)

文件依赖层次说明:
┌─ 配置层: inputs (外部参数配置)
├─ 程序层: main.cpp (程序执行入口)
├─ 接口层: *.H 头文件 (类定义、函数声明、常量定义)
├─ 实现层: *.cpp 源文件 (具体功能实现)
└─ 核心层: D3Q19.H (底层物理模型参数)

信息来源关系:
• main.cpp → 从inputs读取配置，从所有.H文件获取接口定义
• AmrCoreLBM.cpp → 从AmrCoreLBM.H获取类接口，从D3Q19.H和Kernels.H获取计算参数
• LagrangeParticleContainer.cpp → 从对应.H获取类接口，从D3Q19.H获取物理参数，从Kernels.H获取计算函数
• AuxiliaryPointContainer.cpp → 从对应.H获取类接口，从D3Q19.H获取模型参数
• Kernels.H → 从D3Q19.H获取LBM模型的基础常量和参数定义
```

1.  程序从 `main.cpp` 开始，读取 `inputs` 文件中的配置。
2.  `main` 创建一个 `AmrCoreLBM` 类的实例，该类是程序的核心对象。
3.  `AmrCoreLBM` 负责创建和管理流场数据 (`MultiFab`) 以及 `LagrangeParticleContainer` 对象。在初始化颗粒时，会调用 `InitParticles.H` 中的函数来生成初始坐标。
4.  `main` 中的时间循环通过调用 `RohdeCycle` 或 `JaberCycle` 等函数，命令 `AmrCoreLBM` 对象向前演化。
5.  `AmrCoreLBM` 类的方法负责管理 AMR 网格和数据，并调用 `Kernels.H` 中定义的具体计算核函数来处理每个网格点上的碰撞、迁移、边界条件等。同时，它也负责调用颗粒容器中的函数，实现流固耦合计算和颗粒运动更新。
6.  `Kernels.H` 在计算中（如计算平衡态分布函数）会使用 `D3Q19.H` 中定义的模型参数。
---

## 如何编译与运行 (通用指南)

本项目使用 GNU Make 和 AMReX 的编译框架。

1.  **编译**:
    在项目根目录下，通常会有一个 `GNUmakefile`。执行以下命令：
    ```bash
    make -j [N]
    ```
    其中 `[N]` 是您希望使用的并行编译的CPU核心数。这将生成一个可执行文件，例如 `main3d.gnu.MPI.CUDA.ex`。

2.  **本地运行**:
    运行模拟需要 `inputs` 文件和可执行文件。
    ```bash
    ./main3d.gnu.MPI.CUDA.ex inputs
    ```
    对于并行运行（需要预先配置好 MPI 环境）：
    ```bash
    mpirun -np [M] ./main3d.gnu.MPI.CUDA.ex inputs
    ```
    其中 `[M]` 是您希望使用的进程数。

3.  **在HPC集群上提交作业**:
    在高性能计算集群上，通常不直接使用 `mpirun`，而是通过作业脚本提交任务。
    *   **脚本**: `submit.sh` 是一个示例作业提交脚本。
    *   **作用**: 该脚本负责向集群的作业调度系统（如 Slurm, PBS, LSF 等）申请计算资源（CPU, GPU, 内存），加载所需的环境模块，并执行 `mpirun` 命令。
    *   **提交**: 使用集群提供的命令提交作业，例如：
    ```bash
    dsub submit.sh
    ```
    请根据您所使用集群的具体指令和 `submit.sh` 中的配置进行调整。 