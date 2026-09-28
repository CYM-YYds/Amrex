# BOX3D_OSI 新层初值统一

- 目的：消除 A-B/OSI 在第 32 步新建细层时因不同插值和非平衡缩放流程导致的初值差异。
- 修改文件：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/AmrCoreLBM.cpp`；该算例的 `docs/current_status.md`。
- 主要改动：A-B 的 `MakeNewLevelFromCoarse()` 改用已有的 `FillNewLevelFromCoarse()`；该函数根据存储模式读取粗层 DDF，随后共享宏观量刷新、非平衡缩放和由 `lbm.interp_mode` 选择的细层插值。临时取样和快照探针在验证后撤回。
- 验证：GPU/MPI 最终版本编译成功；旧版独立作业 `603468`/`603469` 在插值前测得细层 valid 差异，修正试验作业 `603471`/`603472` 将通信后同层源/ghost 最大差降至 `1.11e-16`，`603473`/`603474` 将平均后 interface 最大差降至 `2.22e-16`。最终无临时探针版本的 `603478`/`603479` 在第 33 步已知 uncovered 点逐值相等。单 rank 33 步验证不涵盖长程稳定性和多 rank 通信。
