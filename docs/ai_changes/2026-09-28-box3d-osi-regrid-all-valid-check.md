# BOX3D_OSI 重构阶段全层级 valid 检查

- 目的：让网格重构阶段的 A-B/OSI 对照覆盖所有活动层级的全部 valid cell，包括 coarse 层的 covered/interface，而不是复用只检查 uncovered 的阶段比较器。
- 修改文件：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/AmrCoreLBM.H`、`src/AmrCoreLBM.cpp`、`src/OsiAbVerification.cpp`、`scripts/submit_osi_lockstep_alllevels_64.sh`。
- 主要改动：增加 `CheckOsiReferenceAllValid`；为阶段比较器增加 `skip_covered` 开关；重构后和首次 FillGhost 后按 level 逐层检查有限值与全部 valid DDF；测试目标改为第 64 步。
- 验证：远程 CUDA+MPI 编译成功；作业 604927 完成。第 64 步重构后 `finest_level=2`，level 1/2 的全部 valid DDF 比较为 `linf=0`；level 0 在重构前的 StepEntry 已有 covered 区域差异，重构后仍报告 `linf=0.29072379259002423`，首个记录点为 logical `(48,20,62)`、`q=0`，因此不能宣称重构后全层级一致。首次 FillGhost 时 level 0/1 全部 valid 比较均为 `linf=0`。
