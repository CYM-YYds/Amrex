# 平均诊断 probe 结构重构

- 目的：整理 `AverageDownInterfaceLevel()`，将平均前后数据导出的诊断逻辑与生产平均流程分离。
- 文件：`src/AmrCoreLBM.cpp`、`src/AmrCoreLBM.H`。
- 主要改动：新增 `ProbeAverageInterfaceLevel()` 成员函数，集中处理 DDF、covered/interface mask 回拷和 probe 文件输出；主流程只保留首次触发条件以及 before/after 调用。
- 验证：`git diff --check` 通过；编译节点两次 SSH 连接均被远端关闭，未完成本次编译验证。
