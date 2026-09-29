# 2026-09-29 使用 interface_mask 构造平均缓存

## 目的

消除 `BuildAverageCache()` 中由 coarse/fine 几何关系重复推导 interface 范围的逻辑，改用已构造的 `interface_mask` 作为平均范围真相，同时保留每个 coarse Box 对应的 fine Fab 索引。

## 修改

- `src/AmrCoreLBM.cpp`：在 `cf_mask_mode=1` 时将 `interface_mask` 复制到按 fine Fab 粗化布局，生成不重叠的 mask-derived Box，并通过 `fine_index` 恢复数据归属。
- 保留 `cached_cells == interface_cell_counts` 和单 fine Fab 归属断言。
- `cf_mask_mode=0` 保留几何回退路径。

## 验证

- `GEN_CCDB=0 ./scripts/compile.sh --no-submit` 编译成功。
- job `605352`：OSI、三层动态 AMR、1000 步，状态 `SUCCEEDED`。
- 运行经历 61 次缓存重建、6 次 regrid；无 NaN/Inf/Abort，mask 覆盖断言未失败。
- step 1000：`average=3.3025 s`，`total=28.6127 s`，`MLUPS_total=1327.05`。
