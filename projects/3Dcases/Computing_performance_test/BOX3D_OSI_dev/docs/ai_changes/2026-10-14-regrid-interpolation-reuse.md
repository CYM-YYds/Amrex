# 重构稀疏 patch 复用公共初始化插值

- 目的：移除 InterpolateOsiFinePatchFromCoarse 中重复的 coarse 模板、搬运、
  物理边界、非平衡缩放和三种插值核分派。
- 改动：保留空 patch 返回、ratio 断言和零 ghost fine patch，调用
  InitializeDdfFromCoarse，最后只把 patch valid 写回 phase=0 目标层。
  不改写已迁移的旧 fine 数据；不移除布局不同所需的稀疏中转。
- coarse 物理延拓统一到公共路径；用户确认该延拓已经测试通过。
  公共模板覆盖检查也适用于此次重构插值。
- 验证：生产 CUDA+MPI 编译链接与 CRLF 感知 diff --check；日志位于
  runs/regrid_interp_reuse_20261014/compile_console.log。此次未新增运行回归，
  不将用户确认的物理延拓测试解释为本提交完成动态 AMR 数值回归。
  现场 AmrCoreLBM_amr.cpp 的用户已有修改保留，未纳入提交。
