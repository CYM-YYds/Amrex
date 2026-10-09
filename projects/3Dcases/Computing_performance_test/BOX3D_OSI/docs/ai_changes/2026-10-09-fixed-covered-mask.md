# 固定启用粗细覆盖掩码并简化平均后修复

- 目的：删除只有值 1 有效的 `lbm.cf_mask_mode` 参数，直接复用 covered 掩码筛选平均后的边界修复格点。
- 范围：仅 BOX3D_OSI 的输入配置、参数读取/输出、掩码构建、推进和诊断；其他算例不变。
- 改动：删除参数成员和解析入口、无掩码分支及相关诊断条件。覆盖掩码固定构建；碰撞保留 interface 例外，迁移和正常边界处理继续跳过 covered。`ApplyPhysicalBoundaryLevel()` 使用 `repair_after_average` 区分正常推进与修复：正常推进跳过 covered，修复跳过 uncovered；修复仍只处理粗层 valid 物理边界。删除细层 BoxArray 粗化、求交和临时修复盒列表。
- 编译：`GEN_CCDB=0 ./scripts/compile.sh --no-submit` 完成 CUDA+MPI 构建，见 [编译日志](../../logs/compile/compile-20261009T145941.log)。运行源码、生产 inputs 和脚本中均无 `cf_mask_mode` 引用；历史变更记录和运行快照保留原貌。
- 逐值验证：GPU 作业 `610641` 成功。单 rank、双 rank 各 12 组非均匀 DDF 检查，覆盖 A-B/OSI、非零 phase、单层/两层/三层、单 Fab/多 Fab、贴壁/域内加密和全周期。平均最大误差 `1.665334537e-16`；修复边界公式误差、目标外 valid/ghost 改变量及 OSI/A-B 差异均为零。
- 回归：双 rank、64 步动态 AMR，基线使用修改前可执行文件，新版本分别运行 direct 和 canonical fallback；每组 1250 条阶段比较均为 `linf=0`。新版本两条路径与基线 checkpoint 的 level 0/1/2、每层全部 27 分量逐值比较均为零，包含 covered valid，全局 `linf/l1/l2=0`。输入、命令、可执行文件指纹、源码差异和日志见 [job610641](../../runs/fixed_covered_mask_20261009/job610641)，汇总见 [validation_summary.json](../../runs/fixed_covered_mask_20261009/job610641/validation_summary.json)。
- 限制：实际写入范围保持不变，但修复改用掩码筛选，核发射候选格点数可能变化；未据此声称性能提升或物理边界离散阶数已验证。本记录接续此前完整平均后边界修复记录，描述当前实现。
