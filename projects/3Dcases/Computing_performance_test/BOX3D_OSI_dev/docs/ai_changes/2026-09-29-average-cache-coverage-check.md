# 2026-09-29 集中平均缓存覆盖检查

## 修改

- 将 `BuildAverageCache()` 中的平均 Box 归属、cell 计数和 `interface_mask` 覆盖断言集中到 `ValidateAverageCacheCoverage()`。
- 主缓存构造逻辑只保留一个诊断函数调用；不需要检查时可注释该调用，不影响 Box 生成和平均路径。

## 验证

- `GEN_CCDB=0 ./scripts/compile.sh --no-submit` 编译成功。
