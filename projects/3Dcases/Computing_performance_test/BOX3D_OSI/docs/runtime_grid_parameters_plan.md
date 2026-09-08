# BOX3D_OSI 运行时网格参数化实施计划

## 目标

将网格尺寸和物理几何参数从源码常量迁移到 `config/inputs`，使同一个可执行文件能够运行不同的网格和物理域，同时保持默认配置的数值行为不变。

本计划只针对以下运行时参数：

- `amr.n_cell`：粗层网格的 `NX/NY/NZ`
- `geometry.prob_lo`：物理域下界
- `geometry.prob_hi`：物理域上界
- `geometry.is_periodic`：各方向周期性

`D`（物体直径）、D3Q27 速度集、权重、`Q`、空间维数和编译支持的最大 AMR 层数暂不迁移。

## 当前状态

- `geometry.prob_lo`、`geometry.prob_hi` 和 `geometry.is_periodic` 已由 `inputs` 读取。
- 参数读取入口已集中到 `main.cpp::ReadInputConfig()`。
- 当前 `NX/NY/NZ` 仍由 `D3Q19.H` 中的 `D` 推导。
- 已验证直接使用可变 host 全局变量会导致 CUDA device 编译错误，因此不能采用该方案。
- 默认构建配置使用 AMReX 26.06、C++20、CUDA、MPI。

## 设计原则

1. `inputs` 是运行时配置来源；源码只保留默认值、派生关系和合法性检查。
2. `Geometry`、AMR、粒子模块和 GPU kernel 必须使用同一份网格参数。
3. GPU 代码不得直接读取通过 `ParmParse` 修改的 host 全局变量。
4. 传入 device 的参数结构保持小而平坦，只包含标量和固定大小数组。
5. 物理模型常量与算例运行参数分离；不把 D3Q27 离散速度等编译期模型数据参数化。
6. 默认 `128 x 128 x 128` 配置必须在迁移前后保持数值一致。

## 目标数据结构

在 `D3Q19.H` 或专用配置头文件中定义可复制到 device 的轻量结构：

```cpp
struct LbmGridParams {
    int nx_cells;
    int ny_cells;
    int nz_cells;
    Real x_length;
    Real y_length;
    Real z_length;
    Real dx;
    Real dt;
    Real dx_min;
    Real dt_min;
};
```

该结构由主机端读取 `inputs` 后一次构造，并按值捕获到 AMReX GPU lambda 或作为 kernel 参数传递。不得重新引入 `NX/nx/dx_min` 等可变 host 全局变量。

## 实施阶段

### 阶段 0：基线冻结

- 保存当前工作区和可执行文件时间戳。
- 使用默认 `inputs`、单 MPI rank、单 GPU、`stream_mode=0` 运行短基线。
- 记录初始状态、前若干步 DDF checksum、宏观量范围和 regrid 事件。

### 阶段 1：配置结构和输入校验

- 在 `ReadInputConfig()` 中读取 `amr.n_cell`。
- 检查三个尺寸为正数，并检查与 blocking factor、refinement ratio 的兼容性。
- 从 `prob_hi - prob_lo` 计算物理域长度。
- 构造 `LbmGridParams`，统一计算 `dx/dt/dx_min/dt_min`。
- 启动时打印最终配置，便于复现实验。

### 阶段 2：Geometry 和 AMR 入口

- 使用 `grid.nx_cells/grid.ny_cells/grid.nz_cells` 构造 coarse `Box`。
- 使用 `prob_lo/prob_hi` 构造 `RealBox`。
- 保持 AMR refinement ratio、最大层数和 BoxArray 逻辑不变。
- 确认 `Geometry` 的 cell size 与参数结构中的 `dx` 一致。

### 阶段 3：主机端派生量

- 将 `dx_0`、`dt_0`、`tau_0`、`mv_0` 等从宏替换为基于 `LbmGridParams` 的小型 `AMREX_GPU_HOST_DEVICE` 函数或显式计算。
- 检查 `D` 相关派生量（半径、粒子质量、惯量）是否仍按原有物理定义计算。
- 不改变碰撞、推进、插值和 AverageDown 的算法顺序。

### 阶段 4：GPU kernel 参数传递

- 找出 `Kernels.H` 中直接使用网格全局量的 device 函数。
- 为这些函数增加 `const LbmGridParams& grid` 参数，或在调用 lambda 中捕获按值复制的结构。
- 优先处理 `dx_0/dx_min/dt_min` 使用点，再处理需要 cell 数量的边界或几何判断。
- 保持 kernel 参数结构紧凑，编译后检查寄存器和 occupancy 是否出现异常变化。

### 阶段 5：粒子和边界逻辑

- 将 `LagrangeParticleContainer` 中直接使用 `NX/NY/NZ` 的边界判断改为使用参数结构或 `Geometry` domain 长度。
- 明确粒子坐标是 cell index、coarse physical coordinate 还是 level-local coordinate。
- 对非立方网格和非零 `prob_lo` 做最小冒烟测试。

### 阶段 6：清理和文档

- 删除不再使用的网格全局变量和宏。
- 检查完整源码、`inputs`、脚本和文档中是否仍存在过时的 `NX = 4 * D` 假设。
- 更新算例 README，说明 `amr.n_cell` 与 `geometry.*` 的关系和限制。

## 验证矩阵

### 编译验证

- 默认 CUDA + MPI 构建成功。
- 不出现 host variable in device code 错误。
- 检查 `git diff --check` 和编译日志。

### 数值验证

1. 默认配置 `128 128 128`：与迁移前比较初始 DDF、前 300 步 checksum、宏观量和 regrid 后状态。
2. 非默认立方网格，例如 `96 96 96`：确认 Geometry、AMR 和输出尺寸一致。
3. 非立方网格，例如 `96 128 160`：确认三方向尺寸没有交叉使用。
4. 非零物理域下界：确认坐标和边界判断没有误用 cell index。
5. 周期和非周期边界各运行一次短测试。
6. 粒子启用时比较粒子位置、速度、力和 checkpoint checksum。

### 性能验证

- 比较默认配置下 kernel 注册器数量、MLUPS 和主要阶段耗时。
- 若参数结构导致明显寄存器增长，改用按需传递的标量或拆分结构。

## 明确不在本计划中的内容

- `D` 或物体半径的运行时化。
- D3Q19/D3Q27 模型运行时切换。
- `nghost` 运行时化。
- 移动刚体 restart 状态持久化。
- 任何 `stream_mode=0` 与 `stream_mode=1` 的算法合并。

## 回滚条件

出现以下任一情况时，停止后续迁移并回退当前阶段：

- 默认配置无法通过 CUDA 编译；
- 默认配置前 300 步出现第一处逐网格差异；
- regrid 后出现 NaN、Inf、非正密度或粒子越界；
- 非默认网格导致 Geometry 与 kernel 使用的尺寸不一致；
- 默认配置性能出现无法解释的明显退化。

