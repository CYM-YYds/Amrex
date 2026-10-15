# AMR 插值函数命名统一

## 修改目的
统一粗细网格插值相关函数的命名语义，区分完整初始化、实际插值、ghost 填充和物理边界处理。

## 修改文件
- `src/AmrCoreLBM.H`
- `src/AmrCoreLBM.cpp`
- `docs/current_status.md`

## 主要改动
- `FillNewLevelFromCoarse` 重命名为 `InitializeNewLevelFromCoarse`。
- `InterpolateNewFineBatch` 重命名为 `InterpolateCoarseBatchToFine`。
- `FillOsiFinePatchFromCoarse` 重命名为 `InterpolateOsiFinePatchFromCoarse`。
- `FillCoarseInterpolationStagePhysicalBoundary` 重命名为 `ApplyPhysicalBoundaryToInterpolationStage`。
- 同步所有声明、定义、调用点和状态文档。

## 验证结果
- 旧函数名在 BOX3D_OSI 源码和文档中无残留。
- `git diff --check` 通过。
- 算例编译脚本已执行；远程编译节点因当前环境 UID 无对应用户而未实际启动编译，未发现编译器错误输出。
