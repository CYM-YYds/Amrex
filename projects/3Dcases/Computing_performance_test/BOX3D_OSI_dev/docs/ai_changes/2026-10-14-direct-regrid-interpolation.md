# 重构插值直接写目标 valid

- 目的：移除 OSI RemakeLevel 的 fine_patch 临时数组及随后的 ParallelCopy。
- 实现：FPinfo 缺失 patch 与目标 BoxArray 求交，保存 work_boxes/fine_indices；
  BuildInterpolationStage 按目标 Fab owner 建立 coarse stencil，现有插值核直接
  写 new_osi_state。空工作区跳过，目标 phase=0，ghost 不在写入范围。
- InitializeDdfFromCoarse 增加可选区域/目标索引参数；省略参数时仍初始化全部
  valid。复用 coarse 搬运、物理延拓、缩放及模板覆盖门禁；coarse staging 保留。
  独立初始化测试夹具声明同步。用户已有的其他 amr.cpp 修改未纳入提交。
- 验证：生产 CUDA+MPI 构建通过，日志 runs/direct_remake_20261014/compile_console.log。
  作业 613414 / SUCCEEDED：1/2 rank 缓存测试通过；mode 0 单/双 rank、mode 1/2
  双 rank，共四组改动前后 64 步动态 AMR，第 32/64 步建立至 level 2。
  全部 checkpoint valid_linf=0，平均探针文件一致；额外 SHA256 比较确认四组
  共 33 个 f_old checkpoint 头文件/数据文件逐字节一致。
- 证据边界：比较日志中部分层 valid_l1/valid_l2=nan，不能仅凭 valid_linf=0
  宣称全部 valid 有限。文件逐字节一致证明此次重构没有改变冻结窗口的 DDF
  存储，不证明既有 covered 数据有限或长期物理正确。运行证据与完整 source.diff
  保留在 runs/cpc_cache_613414/；构建/运行包含用户现场修改，基线为修改前 app。
- CRLF 感知 diff --check 通过。本次未测量速度收益。
