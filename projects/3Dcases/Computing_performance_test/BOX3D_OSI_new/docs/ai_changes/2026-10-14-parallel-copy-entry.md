# 统一 OSI 粗细层搬运入口

- 目的：插值与界面平均只调用 ParallelCopyOsi，集中本地/MPI 分派与相位地址映射。
- 范围：AmrCoreLBM.H、AmrCoreLBM_osi.cpp；新增 CpcCache<SourceRaw> 缓存重载。
  原 MultiFab 重载继续用于调用内临时迁移；缓存重载复用既有 tag、plan、buffer
  与当前 Fab 相位表，不退回逐步重建 CPC。底层 local/MPI 执行函数仅在入口内部调用。
- 插值 raw→canonical 与平均 canonical→raw 的公式、计时字段和原同步行为保留。
  代码中的 AmrCoreLBM_amr.cpp 用户已有修改保留，不纳入本次提交。
- 验证：GEN_CCDB=0 MAKE_J=4 ./scripts/compile.sh --no-submit 完成 CUDA+MPI
  编译链接；日志 runs/parallel_copy_entry_20261014/compile_retry.log。
  首次编译暴露 NVCC constexpr-if 隐式捕获限制，改为显式捕获相位指针后通过。
  CRLF 感知 diff --check 通过。本次按小重构范围仅编译，未新增运行回归；
  此构建包含现场用户已有的 amr.cpp 修改，不作为本提交独立数值等价证据。
