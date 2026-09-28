# BOX3D_OSI 每 32 步 uncovered 锁步检查

- 目的：在首个差异尚未重新锁定前，每 32 步对所有现有 AMR 层级的 uncovered valid DDF 做 A-B/OSI 比较；重构后的全 valid 检查继续按重构阶段执行。
- 修改文件：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/OsiAbVerification.H`、`src/OsiAbVerification.cpp`、`src/AmrCoreLBM.H`、`src/AmrCoreLBM.cpp`、`scripts/submit_osi_lockstep_alllevels_64.sh`、`scripts/submit_osi_lockstep_alllevels_128.sh`。
- 主要改动：增加 `osi_step_entry_check_interval` 参数和 `CheckOsiReferenceUncovered`，避免定期首差异扫描混入 level 0 covered 逐点结果；增加 128 步测试脚本，配置每 32 步检查并在 uncovered 差异时立即失败。
- 验证：远程 CUDA+MPI 编译成功；作业 604946 完成。第 32、64、96、128 步分别检查了 finest level 0、1、2、2，所有层级 `StepEntryUncovered` 均为 `linf=0`，未出现 uncovered 失败记录；第 64 步和第 128 步重构/平均阶段的 valid 检查也均为 `linf=0`。
