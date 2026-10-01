# AverageDownInterfaceLevel 层级参数语义调整

## 修改目的
让平均下传接口的参数直接表示当前细层，避免调用处把粗层目标层误认为当前推进层。

## 主要改动
- `AverageDownInterfaceLevel(fine_lev, ...)` 现在明确将 `fine_lev` 平均到 `fine_lev - 1`。
- `Cycle2`、`JaberCycle` 及多粒子路径在细层递归完成后传入 `lev + 1`，保持原来的数据方向和执行时机。
- 函数内部以 `coarse_lev = fine_lev - 1` 访问缓存和粗层数据。

## 验证结果
- 已检查全部调用点和 coarse/fine 数据索引。
- `git diff --check` 通过（按仓库 CRLF 设置检查）。
- 当前环境远程编译节点无法启动，尚未完成编译验证。
