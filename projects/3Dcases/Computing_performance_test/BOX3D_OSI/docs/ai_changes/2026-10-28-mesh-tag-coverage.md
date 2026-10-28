# 网格信息增加原始标记与覆盖比例

## 目的与范围

在 BOX3D_OSI 的 `PrintMeshInfo()` 中为 level >= 1 输出原始加密标记相对最终细层覆盖的比例。仅修改 `src/AmrCoreLBM.cpp` 的信息输出，不改变打标、建网格或数值推进。

## 实现与统计口径

- 复用已有 `regrid_tag_counts`：常规 `ErrorEst()` 在缓冲扩张、聚类之前统计各父层 valid 单元中的 SET 标记，并进行 MPI 全局求和；不新增状态或重新执行打标。
- `parent_covered_cells` 为当前细层 valid 网格单元数除以逐方向细化倍率乘积，不包含 ghost。
- `tagged_coverage_pct = 100 * raw_tag_cells / parent_covered_cells`。
- `redundant_coverage_pct = 100 - tagged_coverage_pct`。
- 常规重构开始时已有逻辑清空计数，随后由 ErrorEst 更新；初始建网格复用同一路径。checkpoint 不保存该计数，因此重启至首次常规重构之前显示 N/A；受控 regrid_fixture 路径没有计数，也显示 N/A。
- 分子是原始标记总数，不是经 proper-nesting 等约束筛选后、落入最终覆盖范围的标记交集；若原始标记被筛除，比例可能超过 100%，不截断、不将其解释为严格的集合冗余率。

## 验证

- 本次文件的 `git diff --check` 通过；工作区其他已有修改不包含在本次提交中。
- `GEN_CCDB=0 MAKE_J=8 ./scripts/compile.sh --no-submit` 三维 DOUBLE CUDA 编译及链接通过；日志为 `logs/compile/compile-20261028T194120.log`。编译包含工作区已有修改，提交只包含本次信息输出和记录。
- 本次只增加既有全局计数的只读输出，不增加 GPU 内核或 MPI collective；不作数值等价或性能验证声明。
