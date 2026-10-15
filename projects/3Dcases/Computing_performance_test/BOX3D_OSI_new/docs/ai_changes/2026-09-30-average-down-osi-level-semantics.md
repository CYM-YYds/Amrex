# AverageDownOsiLevel 层级参数语义统一

## 修改目的
让 OSI 平均下传内部函数与公共平均接口保持一致，参数始终表示提供细层数据的 `fine_lev`。

## 主要改动
- `AverageDownOsiLevel(fine_lev, ...)` 内部计算 `coarse_lev = fine_lev - 1`。
- OSI 的 coarse state、fine state、phase、缓存和松弛时间索引均按该语义重写。
- `AverageDownInterfaceLevel` 传入 `fine_lev`，保持 A-B 与 OSI 路径使用同一层级约定。

## 验证结果
- 已检查唯一调用点和所有 coarse/fine 索引。
- `git diff --check` 通过（按仓库 CRLF 设置检查）。
- 当前环境远程编译节点无法启动，尚未完成编译验证。
