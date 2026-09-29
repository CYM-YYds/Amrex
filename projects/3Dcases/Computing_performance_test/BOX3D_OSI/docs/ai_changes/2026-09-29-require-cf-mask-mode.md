# 2026-09-29 强制启用 coarse-fine mask

## 修改

- `lbm.cf_mask_mode` 现在只有值 `1` 被接受；缺省或显式设置为其他值时立即终止并报错。
- 删除 `BuildAverageCache()` 中不使用 `interface_mask` 的旧几何回退分支。

## 验证

- `GEN_CCDB=0 ./scripts/compile.sh --no-submit` 编译成功。
- 当前配置 `lbm.cf_mask_mode=1` 保持有效。
