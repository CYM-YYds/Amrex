# 清理 BOX3D_OSI 无调用实现与重复状态

目的：移除当前推进、AMR 与通信路径不消费的旧实现和状态，同时保留算法需要的不同数学内核。

范围：仅本算例 `src/`。删除 `osi_interp_decode_boxes`、无调用的 `FillCoarsePatch` / `FillPatch` / `InterpScale` / `AverageScale`、旧 raw-to-raw 插值、旧 batch/OSI restriction 与逐格点 swap；`stream_mode` 和 `osi_ab_check` 仍可由输入指定，运行时状态统一归于 `RunMode`。四套旧推进函数、三套 cumulant 内核及 Chimera 依赖保持不变。

验证：CUDA+MPI 编译、OSI index、stage-2 A-B、插值覆盖正负例通过。冻结的原版与本次版可执行文件在相同输入、BoxArray 和 1/2 rank 下完成 9 组 64 步动态 AMR/周期多 Fab 对照；全部 9 组的每条 `osi_ab_stage` uncovered `linf=0`，各层 checkpoint valid/active `linf=0`。源诊断仍报告既有 physical-ghost 差异，这不代表 ghost 全等。验证产物位于忽略的 `runs/architecture_dedup_{baseline,stage1}_20261010/`，未清理历史数据。
