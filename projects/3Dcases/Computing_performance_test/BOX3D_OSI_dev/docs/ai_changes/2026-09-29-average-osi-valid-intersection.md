# OSI 平均写回只匹配 coarse valid 区域

- 目的：避免单 rank direct 写回路径把 coarse ghost 区域参与 Fab 目标匹配。
- 文件：`src/AmrCoreLBM.cpp`。
- 改动：`AverageDownOsiLevel()` 中 `BoxArray::intersections()` 使用零 ghost 查询；确定目标 valid Box 后，再用 grown `ring` 计算 OSI raw 地址。
- 验证：`git diff --check` 通过；本次改动为接口参数和注释调整，待下一次目标算例编译确认。
