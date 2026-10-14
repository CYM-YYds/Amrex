# DDF 插值统一使用现有自定义数值核

## 目的与修改范围

按用户要求，将 BOX3D_OSI 新层初始化接到 InterpolateDdfStageToFine，
移除生产 DDF 路径最后一处 FillPatchInterp 数值调用。初始化、运行期
coarse-to-fine ghost 和 regrid 新增区域现在使用相同的三种自定义公式：
ratio-2 三线性、各方向 minmod 守恒线性、中心差分二阶 Taylor 插值。

稀疏模板、缩放、当前 coarse phase 的 raw 搬运以及 fine 物理 ghost 补全
沿用既有机制。初始化目标为 canonical 或 phase=0 OSI，可直接写入。
未新增数值替代路径、缓存或状态，也未修改宏观量/力场插值。

DdfInterpolater 仅用于 AMReX BoxCoarsener 的模板几何，以及 FPinfo 的区域
规划，不调用 AMReX 的 DDF 数值插值器。Kernels.H 的注释同步明确自定义
线性限幅不等同于 AMReX 多分量限幅，不宣称多维无条件最大值原理。

## 验证

- `GEN_CCDB=0 MAKE_J=8 ./scripts/compile.sh --no-submit`：生产 CUDA+MPI 编译通过，
  日志为 `../../runs/custom_interp_20261014/compile_console.log`。
- GPU 作业 **613372 / SUCCEEDED**，证据目录
  `../../runs/sparse_initialization_613372/`，冻结执行文件、SHA256、输入、命令、
  工作区源码差异及独立测试源码。
- 1/2 rank，三种模式，全非周期/全周期/混合周期，coarse phase=0/17/41，
  共 **72 组**；fine valid 与两层 ghost 逐值检查通过且无非有限值。
  mode 0 对照独立 AMReX 公式；mode 1/2 对照完整父层缩放后的独立逐点
  标量 oracle，不调用生产 children kernel。总体最大差异 **2.220446049e-16**。
- 相对旧 AMReX 初始化公式，mode 1 在该非均匀输入上的最大差异约
  **4.07884e-4**，属于明确授权的限幅公式变化；mode 2 在这些测试中的差异
  仅为舍入量级。该结果不推断任意数据/边界下的旧公式等价。
- 父层 valid 模板缺口负例在启动插值前按覆盖检查终止。
- 四组 64 步动态 AMR，regrid_int=32，实际新增 level 1/2：
  mode 0 的 1/2 rank 与修改前执行文件比较，全 valid 最大差异
  **1.110223025e-15**；mode 1/2 的 2 rank 与新版本独立 canonical A-B
  运行比较，**active_linf=0**。新版本合计 **4464 条 osi_ab_stage 的 linf=0**，
  包括重构后覆盖 coarse covered 的 AfterRefineMeshAllValid 检查。
- 首轮作业 613371 因对独立存储模式的 covered checkpoint 状态也要求相等而
  被测试脚本判失败；数值执行已结束，active 区及阶段锁步均通过。
  最终测试保留完整 valid 范数，但 canonical/OSI checkpoint 对照仅约束
  active 区：mode 1/2 的全 valid 最大差异分别约 0.00931062/0.009269，
  分布在 covered 粗单元；最细层全 valid 差异为 0。推进跳过 covered 内部，
  OSI 仍全 Fab 推进 phase，不能把这些未同步的 covered 值当作当前 active 状态。
  本次未改变 covered 生命周期，也不把 active 的结果表述成全部 checkpoint
  valid 状态相等。
- 动态 source 检查仍有既有非周期 physical-ghost source 差异；初始化的完整
  ghost 验证与运行期 source 检查分开报告。
- 新增/调整脚本通过 bash -n；CRLF 感知 git diff --check 通过。

测试入口仍为 `../../tests/build_sparse_initialization_test.sh` 和
`../../tests/submit_sparse_initialization_validation.sh`。AMReX 数值计算仅保留
在独立测试参考端，用于报告公式变化，不属于生产路径。
