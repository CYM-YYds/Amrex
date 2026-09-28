# BOX3D_OSI 重构阶段全层级 valid 检查

- 目的：让网格重构阶段的 A-B/OSI 对照覆盖所有活动层级的全部 valid cell，包括 coarse 层的 covered/interface，而不是复用只检查 uncovered 的阶段比较器。
- 修改文件：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/AmrCoreLBM.H`、`src/AmrCoreLBM.cpp`、`src/OsiAbVerification.cpp`、`scripts/submit_osi_lockstep_alllevels_64.sh`。
- 主要改动：增加 `CheckOsiReferenceAllValid`；为阶段比较器增加 `skip_covered` 开关；重构后和首次 FillGhost 后按 level 逐层检查有限值与全部 valid DDF；测试目标改为第 64 步；修复 `AverageDownOsiValidLevel` 逐个 q 解码时反复写入工作区分量 0 的错误。
- 验证：远程 CUDA+MPI 编译成功；作业 604939 完成。修复前 `AfterAverageDownValid` 的 level 0 covered 区域有 1399216 个不一致值，修复后 level 0 全部 valid 为 `unequal=0`；第 64 步重构后 `finest_level=2` 的 level 0/1/2 全部 valid DDF 均为 `linf=0`。第 64 步通信后剩余 source mismatch 均分类为 physical-boundary ghost，valid/interface/covered mismatch 均为 0。
