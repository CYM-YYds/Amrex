# 统一 OSI Fab 索引初始化

- 目的：消除完整 grown-Fab 环的起点、长度与相位偏移初始化重复代码。
- 范围：仅 BOX3D_OSI 的索引辅助函数、推进、宏观量、粗细传递、通信几何准备及诊断调用。
- 改动：在 `src/OsiIndex.H` 的 `OSI` 命名空间提供 `MakeOsiFabGeometry(ring)` 与 `MakeOsiFabContext(ring, phase)`；后者返回几何与相位偏移。采用 Box 接口模板以保留地址头文件的独立 CPU 测试能力。替换 23 处几何初始化，环范围与地址公式保持一致；碰撞相位仍仅在 OSI 分支内准备。`AmrCoreLBM_detail.H` 保持原状。
- 验证：`tests/run_osi_index_test.sh`、`git diff --check` 通过；算例 `GEN_CCDB=0 ./scripts/compile.sh --no-submit` 完成 AMReX 26.06 / C++20 / CUDA+MPI 编译链接。作业 610666 的 A-B、direct、fallback、periodic 四组均以 2 rank / 2 GPU 运行 64 步正常结束；direct/fallback 为三级非周期域，32 步 regrid，periodic 为单层多 Fab 非均匀初值。direct/fallback 各 1250 条阶段检查、periodic 576 条阶段检查全部 `linf=0`，无 stage failure；独立 A-B checkpoint 对比 direct/fallback 的全局 DDF `linf=1.165734176e-15`。非周期 source 诊断仍存在 physical-ghost mismatch，但 valid/internal-ghost mismatch 为零；周期 source mismatch 为零。这不代表所有非周期 ghost 来源完全等价，也不覆盖长时间、多节点或 device-direct。
- 证据：`runs/osi_context_matrix_610666/` 保存 app、输入、源码 diff、SHA256、各组命令及日志；提交脚本位于 `runs/osi_context_validation_20261009/submit.sh`。此前 610663/610664 因脚本路径/执行权限未启动计算。
