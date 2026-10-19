# 维度兼容访问器与三维专属代码收敛

## 第一步：集中第三速度分量访问

目的：将逐处散落的维度选择收敛到格子数据所有者，同时保持 host/device 地址空间与三维运算顺序。范围仅 BOX3D_OSI_dev；顶盖定义、OSI 三坐标表示、推进、通信及物理模型均沿用现有实现。

- 在 LbmLattice.H 增加 device 的 `lattice_ez(q)` 与 host 的 `lattice_ez_host(q)`，分别读取已有 e/e_host 表，二维返回整数零。调用方保证 `0 <= q < Q`。两个入口解决多处重复的维度与地址空间选择，不新增表、缓存或其他状态；方向和权重仍由原 X-macro 生成。
- 替换 Kernels.H、AmrCoreLBM_osi.cpp、AmrCoreLBM_diagnostics.cpp 及平衡态公式中的 51 处第三分量维度宏。平衡态表达式保留原算术顺序，并补充中文注释；顶盖原分支保持。
- 增补现有局部内核测试，逐个有效方向核对 host/device 入口。新增可复用的生成代码对比和阶段回归脚本，复用既有数值诊断，没有另写数值比较内核。

### 验证

修改前基线：`runs/accessor_refactor_20261019/baseline/`，HEAD 为 `9e65360`，保存源码、配置、CUDA+MPI 构建日志、程序及 SHA256。共享文件系统首次复制程序时报文件被替换，重新复制后与源文件 SHA256 一致，再继续验证。

- 二维、三维 CUDA+MPI 编译及现有 ASan/UBSan 内核测试均通过。编译数据库更新为 DIM=3。
- `tests/compare_dimension_codegen.py` 的 18 项对比全部通过：两个维度各两个 CPU 访问器探针的指令相同；12 个对象文件的 CUDA 指令相同；完整二维 664 个、三维 666 个 GPU 函数的 SASS 指令序列相同。比较忽略汇编排版、指令地址及编码注释，保留操作码和操作数；不声称 ELF 文件逐字节相同，也未做性能计时。原始逐函数指令与汇总在 `runs/accessor_refactor_20261019/codegen_accessors/`。
- 计算作业 **615091**：42 个场景，均正常退出。24 组阶段矩阵覆盖二维/三维、三个插值模式、1/2 rank、方腔/周期边界、多 Fab，64 coarse step；二维实际新建至 level 2，三维至 level 1。26,496 条 `osi_ab_stage` / level-0 pointwise 记录最大误差为零。
- 同作业另含 6 个修改前 A-B 基线和 12 个修改后 A-B/OSI 对照，均运行 192 coarse step；实际触发新建层、Remake、清层及重新建层。最终 active DDF 逐 cell/q 对照最大误差为零。范围遵循现有 covered/interface 和阶段读写规则，不将 active 结论扩大到全部存储。

### 单独报告的既有物理 ghost 差异

通信后、边界处理前的 `osi_ab_stream_sources` 会报告物理域外 ghost 差异；这不是上面的阶段比较范围。方腔场景最大绝对差异为 **0.02019539653**，其 valid、internal ghost、interface、covered 源的差异计数均为零；周期源诊断无差异。不能据此声称所有 ghost 均通过 A-B/OSI 等价检查。

作业 **615092** 用修改前程序、相同输入及 rank/布局复现全部 12 个方腔阶段场景。逐条源诊断的计数和最大误差与修改后相同，确认该现象已存在；本次未修改其数值处理。

615091 的首版汇总把不同范围的 linf 混在一起，因上述物理 ghost 差异返回失败，调度器因此标记 FAILED。修正后的检查器将阶段、active DDF 和物理 ghost 分开，并要求所有 12 个旧程序控制均匹配；重汇总通过。原汇总、原检查器和运行日志保留为 `acceptance_initial_mixed_ranges.json`、`checker_initial_mixed_ranges.py` 等。

最终报告：`runs/dimension_refactor_615091/acceptance.json`，42 个主场景与 12 个基线控制检查通过。复现命令：

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI_dev/runs/dimension_refactor_615091/check_dimension_refactor.py \
  projects/3Dcases/Computing_performance_test/BOX3D_OSI_dev/runs/dimension_refactor_615091 \
  projects/3Dcases/Computing_performance_test/BOX3D_OSI_dev/runs/dimension_refactor_control_615092
```

后续 `tests/submit_dimension_refactor_validation.sh` 已将 12 个旧程序控制直接纳入同一作业，使用运行目录冻结的检查器；可用 DIM_REFACTOR_BASELINE 指定基线目录。Shell 语法、Python 语法及 CRLF 感知的 diff 检查通过。本次结论限于记录的阶段及短程输入，不包含长期收敛、跨节点或 GPU-aware MPI 验收。


## 第二步：三维专属代码及球体常量分组

第一步独立提交为 `638815e`。本步继续在现有文件内整理，没有新增模块：

- AmrCoreLBM.cpp 的粒子管理段先放 5 个共用函数，再将 11 个三维专属函数放在一个连续守卫块；头文件对应声明同步分组。16 个原有函数体逐字相同，原文 SHA256 在 `runs/accessor_refactor_20261019/particle_function_bodies.json`，只调整顺序与外层守卫。
- LbmLattice.H 中 rb、ns、nt1、LP_area 仅在三维定义；共享的 ds0、LP_dr 保留。
- LagrangeParticleContainer::PrintParticleParm 改为明确的二维/三维 `#if / #else`，避免二维仍编译不可达的球体参数打印。圆盘和球体数值定义及打印内容不变。

两个维度再次完成 CUDA+MPI 构建和 CPU ASan/UBSan 内核测试，最终编译数据库为 DIM=3。最终代码再次与原始基线比较生成代码，18 项全部通过，完整二维 664 个、三维 666 个 GPU 函数指令序列仍相同；结果在 `runs/accessor_refactor_20261019/codegen/comparison.json`。两步的生成代码报告均保留。

粒子作业 **615156** 成功，`runs/particle_geometry_615156/acceptance.json` 的 10 个场景、69 项检查全部通过：

- 二维 192 步动态 AMR、1/2 rank、多 Fab，实际新建层、Remake、清层。805 个圆周标记和 180 个辅助点几何检查通过。
- 跨 rank 和跨 rank 重启的全部标记坐标及属性逐值误差为零；连续/重启 active DDF 误差为零，静态粒子/纯流体 active DDF 差异为 `1.554312234e-15`，满足 `1e-12`。
- 错误形状、错误粒子元数据、缺失二维元数据均按预期拒绝。
- 原三维基线使用 `runs/particle_port_baseline_20261016/app3d.ex`，包含旧的无 Geometry 粒子 checkpoint 路径；旧 checkpoint 读取后，203769 个球面标记的坐标及全部属性逐点误差为零。三维最终 active DDF 回归误差为零；辅助 checksum 差异 `2.84217095e-13`，在 `1e-12` 容差内，逐点比较仍严格要求零误差。

对应构建日志在 `runs/accessor_refactor_20261019/structure_build2.log` 与 `structure_build3.log`；局部测试日志为 `structure_dimension_kernels.log`。文档本地链接、CRLF 感知 diff 检查通过。本步没有启用二维 IBM、运动、碰撞或润滑模型，也没有处理上节列出的既有物理 ghost 诊断差异。
