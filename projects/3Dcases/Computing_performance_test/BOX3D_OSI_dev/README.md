# BOX3D_OSI_dev

基于 AMReX 26.06、C++20、CUDA 和 MPI 的二三维 LBM + 动态 AMR 开发算例。
同一套源码在 DIM=2 时使用 D2Q9，在 DIM=3 时使用 D3Q27/order=0。
默认保持三维；原始三维算例位于相邻的 `BOX3D_OSI`。

## 构建与运行

在本算例根目录执行：

```bash
# 默认三维
./scripts/compile.sh
dsub -s ./scripts/submit.sh

# 二维，显式向调度器传入维度
DIM=2 ./scripts/compile.sh
dsub -x 'all;DIM=2;' -s ./scripts/submit.sh
```

编译入口转发到仓库公共脚本，在编译节点加载环境，生成
`main2d.gnu.TPROF.MPI.CUDA.ex` 或 `main3d.gnu.TPROF.MPI.CUDA.ex`。
维度唯一来自构建配置；不要再修改格子头文件选择维度。
实际构建设置见 [config/GNUmakefile](config/GNUmakefile)。

三维默认输入为 [config/inputs](config/inputs)，二维为
[config/inputs.2d](config/inputs.2d)。输入的几何和 `amr.n_cell` 必须恰好有 DIM 个分量，
各有效方向网格间距相等。二维不读取 `amr.blocking_factor_z`。
两者默认 Re=1000、U0=0.05、单位计算域，黏度为 U0×Lx/Re。
二维顶盖为 y 高端，三维顶盖仍为 z 高端，均沿 x 方向运动；顶盖角点采用顶盖速度。

提交入口创建 `runs/<timestamp>_job<id>/`，保存输入、覆盖参数、实际命令、维度、
源码版本和可执行文件 SHA256。可执行文件使用符号链接；严格版本对照应额外冻结
可执行文件与源码。通过 `AMREX_RUN_ARGS` 覆盖运行参数。

默认编译会重新采集当前维度的 `config/compile_commands.json`，不混入另一维度的旧记录；
切换后重启 clangd。`GEN_CCDB=0` 适用于不需要更新编辑器数据库的构建。

## 数值路径与功能边界

- `lbm.stream_mode=0`：canonical A-B 双数组；`=1`：grown-Fab OSI 单数组。
- `lbm.collide_mode=0`：原 BGK 基准；`=1`：A-B/OSI 共用的无外力、无 SGS BGK，OSI 要求此模式。
- `lbm.interp_mode=0`：ratio=2 双/三线性；`=1`：minmod 守恒线性；`=2`：单元二次 Taylor 插值。
- DDF 数值插值使用自定义内核；AMReX 提供 stencil、覆盖和通信几何。新层只初始化 fine valid，ghost 留给后续填充。
- 二维平均使用四个子单元，三维使用八个；两者保留相同的非平衡分布缩放和 `Cycle2` 子循环。
- OSI 沿用三坐标内部表示；二维 z=0、z 环长度=1、z 速度与相位偏移=0，不构造额外 z 单元。
- MPI 默认使用 host staging；设备直传仍要求独立验证的 CUDA-aware MPI 环境。
- 二维支持流体主链和静态圆盘容器；IBM 力、粒子运动、碰撞、润滑、压力采样、SGS 和 cumulant 仍为三维功能。

速度输出在二维为 ux、uy，在三维为 ux、uy、uz。涡量输出仍含 Q 和 Vorticity；
二维使用平面速度梯度，Vorticity 为平面旋度的绝对值。

Checkpoint 保存 canonical DDF，并写独立 `Lattice` 元数据。重启和参考 DDF 对照
在读取维度相关场数据前核对维度、Q、排列。无元数据的旧 checkpoint 只允许
三维 D3Q27/order=0 使用。OSI 重启后 phase 归零，再编码到当前 Fab ring。

核心代码分别位于 [src/main.cpp](src/main.cpp)、[src/AmrCoreLBM.H](src/AmrCoreLBM.H)、
[src/LbmLattice.H](src/LbmLattice.H)、[src/Kernels.H](src/Kernels.H) 和
[src/OsiIndex.H](src/OsiIndex.H)，AMR、推进、OSI 和诊断按实现文件拆分。
历史专项提交脚本保留三维用途；不能仅设置 DIM 就将其作为二维入口。
第三速度分量由格子定义层的 `lattice_ez(q)`（device）与 `lattice_ez_host(q)`（host）
统一访问原表；二维返回零。访问器重构的生成代码与逐值验证见
[维度兼容收敛记录](docs/ai_changes/2026-10-19-dimension-accessors.md)。

## 静态粒子容器

二维默认关闭粒子，可在输入或运行覆盖参数中设置：

```text
checkpoint.write_particles = true
particles.shape = disk
```

三维形状为 `sphere`（默认）。二维显式拒绝 `sphere`、三维 `.lpa` 文件和缺少
圆盘几何元数据的粒子 checkpoint；目前二维外部几何仅支持读取本程序的圆盘 checkpoint。
此开关沿用现有接口，同时控制静态标记初始化和 checkpoint，不施加流固耦合力。

二维圆心默认在计算域中心，直径沿用源码 `D=32` 个 coarse cell，
标记半径为 `R * grid.dx`，不采用球体的有效半径修正。圆周等角采样，数量
`ceil(2*pi*rate*R/ds0)`；二维属性为 `fx, fy, tz, xlocal, ylocal, area`，
其中 `area` 表示以 `grid.dx_min` 为单位的弧长，局部坐标为物理长度。
圆周必须严格位于计算域内部。质量与惯量按单位厚度圆盘定义：
`m=rhop*pi*(D*dx)^2/4`，`Iz=m*(D*dx)^2/8`；当前仅用于几何参数报告。

每逢 `amr.plot_int` 输出时，保存 `particle_0_<五位步数>` 的 AMReX ASCII 标记数据；
二维另输出 `cp_0_<六位步数>` 的 180 个圆周辅助点，`cp=0` 代表尚未执行压力采样。
ASCII 文件前五行是数量及属性布局，后续每行是 DIM 个坐标、ID、CPU、SoA 属性。
二维坐标仅有 x、y。容器复用 AMReX 的多层归属和 MPI 重分配。

粒子 checkpoint 在 `particles_0/Geometry` 保存版本、维度、形状、半径、网格尺度和圆心，
读取标记前先校验；AMReX 保存全部标记坐标及属性。二维重启按保存圆心重建静态辅助点。
旧三维粒子 checkpoint 仍可读。运动刚体的速度/姿态历史重启不在本次范围内。
实现及验证见[圆盘容器变更记录](docs/ai_changes/2026-10-16-particle-disk-container.md)。

## 验证

```bash
bash tests/run_dimension_kernels_test.sh
bash tests/run_osi_index_test.sh
# 静态粒子容器检查：先构建两个维度，并准备脚本指定的修改前三维基线。
# 可用 PARTICLE_BASELINE_EXE 环境变量指定经过核对的基线程序。
dsub -s tests/submit_particle_geometry_validation.sh
# 最近访问器与结构整理的回归：先构建两个维度，基线目录需含两个维度的修改前程序。
# DIM_REFACTOR_BASELINE 默认 runs/accessor_refactor_20261019/baseline。
dsub -s tests/submit_dimension_refactor_validation.sh
# 初次维度迁移的完整矩阵；DIMENSION_BASELINE_EXE 指定经核对的旧三维基线。
dsub -s tests/submit_dimension_validation.sh
```

局部检查直接调用生产内核，验证格子矩、三个插值模式、平均、OSI 地址双射、
碰撞、顶盖角点和涡量，并启用地址及未定义行为检查器。
2026-10-15 的二维 GPU 矩阵覆盖最高层编号 0/1/2/3、三个插值模式、1/2 rank、方腔和周期边界；
另检查独立 A-B/OSI 生产模式、连续/重启一致性、旧三维基线和错误模型拒绝。
容差为 active DDF 最大绝对误差不超过 1e-12，同时检查有限值。

`verification.regrid_fixture=true` 是默认关闭的受控加密夹具，按粗步切换加密区，
触发新建层、Remake、清层及缓存重建。日志 `AMR_LIFECYCLE` 记录实际回调。
`verification.final_state_check=true` 复用已有 uncovered valid 有限值与宏观状态检查。
锁步诊断按阶段区分 physical ghost、internal patch ghost 和 covered/interface；
编译成功、正常结束和全局 checksum 均不能替代逐 cell/q 的数值证据。

回归在 `runs/dimensions_<job-id>/` 保存可执行文件、源码、配置、命令、SHA256、日志、
`status.tsv` 和 `summary.json`。跨版本 checkpoint 对照固定 rank 和布局；跨布局比较
需空间重映射。初次迁移的历史结果见 [维度迁移记录](docs/ai_changes/2026-10-15-dimension-port.md)。

2026-10-19 的最新复核见 [访问器与结构整理记录](docs/ai_changes/2026-10-19-dimension-accessors.md)：
生成代码 18 项一致；作业 615091 的 42 个主场景和 615092 的 12 个旧程序控制完成分范围复核，
阶段与最终 active DDF 对照最大误差为零。该轮二维最高层为 2、三维为 1，
阶段运行 64 步、生产回归 192 步；不能外推为新的三维四层或长期验收。
最终结构整理后的粒子作业 615156 有 69 项检查通过。
现有两个可执行文件的 SHA256 与该作业冻结程序一致。

**保留的警告**：方腔通信后、物理边界处理前的 physical ghost 源诊断仍存在差异，
最大绝对值为 0.02019539653，旧程序控制复现相同指标。valid、internal ghost、
interface 和 covered 源的差异计数为零；这不代表全部 ghost 等价。
源码对 `max_level > 1` 仍输出历史保守警告，须结合维度、阶段和上述记录判断验证范围。
615091 的调度器状态为 FAILED，原因是首版汇总混合了诊断范围；原失败报告保留，
修正检查器后对既有数据重新汇总通过，不能将其描述成调度器成功。
最新整合作业脚本已包含旧程序控制，其合并为单作业的入口尚未重新整套提交；
已有证据来自上述两个作业。
长期二维方腔收敛、文献曲线对比和性能优化不属于本次短程验收。
