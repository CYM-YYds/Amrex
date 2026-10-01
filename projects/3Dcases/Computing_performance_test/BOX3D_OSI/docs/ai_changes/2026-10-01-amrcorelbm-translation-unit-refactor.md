# AmrCoreLBM 实现文件职责拆分

- 目的：降低 `AmrCoreLBM.cpp` 的单文件耦合，保留现有 `AmrCoreLBM` 接口、AMR/OSI 数据语义和推进顺序。
- 范围：`src/AmrCoreLBM.cpp`、`src/AmrCoreLBM_amr.cpp`、`src/AmrCoreLBM_osi.cpp`、`src/AmrCoreLBM_advance.cpp`、`src/AmrCoreLBM_diagnostics.cpp`、`src/AmrCoreLBM_detail.H`、`config/Make.package` 及源码导航文档。
- 主要改动：将 AMR 生命周期、OSI 通信与 phase 映射、生产推进、显式诊断分别编译；基础文件保留构造、参数、输出、checkpoint 和粒子耦合。共享计时器和 checkpoint 格式常量放入内部头文件。
- 验证：`tests/run_osi_index_test.sh`、算例脚本 `bash -n`、AMReX 26.06 + CUDA + MPI 目标编译和链接均通过。作业 `606276` 的 A-B、OSI direct、OSI fallback 和全周期组均运行到 step 64 并正常结束；direct/fallback 的有效区域 staged check 为 `linf=0`、`unequal=0`，全周期 source diagnostics 为零。非周期 direct/fallback 仍报告 physical-ghost source mismatch，但 `mismatch_valid=0`，该现象属于既有诊断边界，不能扩展为所有 ghost 来源等价。
