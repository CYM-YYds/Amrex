# 完整平均后的物理边界修复范围

> 历史实现记录；覆盖掩码参数和平均后修复范围已由 [2026-10-09 固定掩码变更](2026-10-09-fixed-covered-mask.md) 更新。

- 目的：避免 `AverageDownValid()` 后重复改写未被平均覆盖的边界、最细层和同层 ghost 副本。
- 范围：仅 BOX3D_OSI 的 `AmrCoreLBM.H`、`AmrCoreLBM_advance.cpp`；保留现有非平衡外推公式和正常推进边界处理。
- 改动：`RepairCurrentStatePhysicalBoundary()` 只遍历 `lev < finest_level`，使用细层 valid BoxArray 粗化后的区域限定修复。`ApplyPhysicalBoundaryLevel()` 增加可选区域参数，将修复工作盒裁剪到粗层 valid 与该区域的交集；边界核仍只写非周期物理边界格点。OSI 锁步参考态使用同一修复区域；单层时不写状态。
- 编译：`GEN_CCDB=0 ./scripts/compile.sh --no-submit` 完成 CUDA+MPI 构建，最终日志为 [compile-20261009T120755.log](../../logs/compile/compile-20261009T120755.log)。
- 逐值验证：GPU 作业 `610554` 成功。测试驱动链接生产目标文件，使用非均匀 DDF 和独立主机缩放/平均/边界公式；单 rank、双 rank 各 24 组，覆盖 A-B/OSI、非零 OSI phase、单层/两层/三层、单 Fab/多 Fab、贴壁/域内加密和全周期。平均结果最大误差 `1.665334537e-16`；修复区域的边界公式误差、目标外所有 valid/ghost 的改变量以及 OSI/A-B 差异均为零。
- 推进回归：双 rank direct、canonical fallback 分别运行 64 步，32/64 步动态重网格建立到 level 2；每组 1250 条阶段比较记录均为 `linf=0`，平均后和修复后的 level-0 逐值检查亦通过，无阶段失败或非有限值标记。输入、命令、源码差异、可执行文件指纹和日志保存在 [job610554](../../runs/covered_boundary_repair_20261009/job610554)，汇总见 [validation_summary.json](../../runs/covered_boundary_repair_20261009/job610554/validation_summary.json)。
- 证据边界：生产入口当前只接受 `cf_mask_mode=1`；函数级测试额外设置无掩码状态，验证修复范围不依赖掩码。此变更验证修复范围和当前边界公式，不证明壁面离散精度阶数或任意 ghost 来源正确。初始化和重网格插值的完整边界重建不属于该平均后修复函数的职责。
