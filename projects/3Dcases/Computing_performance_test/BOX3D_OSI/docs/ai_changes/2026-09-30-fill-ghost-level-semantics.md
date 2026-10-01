# FillGhostLevel 层级参数语义调整

## 修改目的
让时间推进调用处始终传入当前递归层 `lev`，由 `FillGhostLevel` 根据填充模式确定目标层。

## 修改文件
- `src/main.cpp`
- `src/AmrCoreLBM.H`
- `src/AmrCoreLBM.cpp`

## 主要改动
- `FillGhostLevel` 始终把当前 `lev` 作为粗层并填充 `lev+1`；`is_scale` 不再决定是否插值。
- `FillDdfGhostFromCoarse` 和 `FillOsiGhostFromCoarse` 增加 `apply_scale` 参数，粗到细插值始终执行，仅在该参数为 true 时调用 `average_scale`。
- `RohdeCycle` 和 `RohdeCycleMultiParticle` 的旧调用暂未重构，已在调用点注明其层级语义风险。
- 首次 ghost 探针继续以实际填充层判断和输出。

## 验证结果
- 已检查所有调用点和目标层映射，保持原行为。
- `git diff --check` 通过。
- 当前环境的远程编译因 UID 无对应用户而无法启动；未完成编译或运行验证。
