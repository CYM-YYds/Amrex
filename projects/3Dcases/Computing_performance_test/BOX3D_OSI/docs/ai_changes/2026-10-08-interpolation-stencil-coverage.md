# 粗到细插值缓存的模板覆盖检查

- 目的：在插值缓存建立时拒绝父层 valid 无法提供完整模板的布局，避免 staging
  中的域内缺口被后续缩放、插值使用。
- 范围：本算例 `BuildDirectInterpolationCache()` 的共享几何路径，覆盖 A-B、OSI
  direct 和 canonical fallback 使用的普通 ghost 插值缓存。未修改插值核、DDF
  缩放、ghost 读取宽度、时间推进、平均算法或生产输入参数。
- 主要改动：新增 [InterpolationCoverage.H](../../src/InterpolationCoverage.H)，
  使用父层 valid 并集及周期像检查实际 `BoxCoarsener` 模板。非周期域外坐标按现有
  物理边界延拓的钳制规则映射到域内，再检查来源；墙面、棱边和角点不会仅因模板
  伸出物理域而报错。存在缺口时 `amrex::Abort` 输出层号、fine Fab、work box、
  coarse stencil、缺失 boxes、插值模式、ghost 宽度和当前 `amr.n_proper`。
- 报错提示：考虑增大 `amr.n_proper`（proper nesting），核对 blocking factor、
  refinement ratio 和模板宽度后重新生成网格；或接入多层递归补缺
  `amrex::FillPatchNLevels`，适配 DDF 缩放和 OSI 布局。代码没有自动执行递归补缺。
- 验证：`GEN_CCDB=0 MAKE_J=8 ./scripts/compile.sh --no-submit` 完成 CUDA+MPI
  编译，最终复核日志为 `logs/compile/compile-20261008T192211.log`。在编译节点 GCC 11.3
  环境运行 `bash tests/run_interpolation_coverage_test.sh`，13 项几何与返回路径
  检查通过；缺口负例确认由覆盖检查终止，并验证定位信息和修复提示。
  用例包括多 Fab 并集、三向周期、周期像、混合边界、墙外延拓来源、域内缺口和
  精确的 64 个缺失单元。脚本 `bash -n` 与 `git diff --check` 通过。
- 证据边界：本次验证是几何覆盖门禁的回归及生产编译，不是 DDF 逐值推进或长期
  数值正确性验收。检查不证明来源值的时间状态或数值正确，也不独立检查新建层与
  regrid 临时插值 patch 的读取；初始化、重构或 restart 重建普通缓存时会执行它。
  用户的方案文档删除和 `FillCoarsePatch` 注释删除修改保留在工作区，未纳入本次提交。
