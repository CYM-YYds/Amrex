# Kernels.H 的 clangd CUDA 内建函数解析修复

- 目的：消除普通 C++ 索引模式下 `__shfl_down_sync` 和 `__shfl_sync` 未声明的诊断。
- 范围：仅本算例 `.clangd`，不修改源码、编译数据库或生产构建参数。
- 改动：为直接解析 `Kernels.H` 添加两个保留 value 参数的占位宏；这些宏仅供编辑器类型解析，不模拟 warp 通信。
- 验证：使用工作区配置的 clangd 18 和 GCC query-driver，修复前在 2858、2860 行复现两条未声明诊断；修复后完成 preamble、AST 和索引构建，无解析错误。后续耗时的逐 token 编辑器功能测试主动终止，没有将其视为完整 check 成功。`git diff --check` 通过。
- 限制：当前仍使用普通 C++ CUDA 兼容索引模式，不能据此验证设备执行语义；未重新构建或运行数值算例。
