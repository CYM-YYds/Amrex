# 按 Fab 共享 OSI 相位偏移

目的：将持久通信路径中的格点级取模移到每层本地 Fab 的预计算表，同一 Fab 的多个 tag 和 Q 分量共享偏移。

范围：仅 BOX3D_OSI 的状态生命周期、Stream、同层通信、插值搬运、平均写回及相关测试；物理模型、推进顺序、输入和构建配置不变。

主要改动：

- `OsiCommunication.H` 中新增 `FabPhaseCache`，由 OSI MultiFab 的完整 grown-Fab 几何生成紧凑本地表；全局 Box 编号只用于构建索引。保存布局及相位标记，用断言检查错误布局和过期相位。
- `InitializeOsiLevel` 建立相位 0 的表，覆盖新层、regrid 和 canonical restart；OSI Stream 增加相位后在当前 GPU stream 更新表；清除层时清除表。相位唯一来源仍为 `osi_phase`。
- FB/CPC 的持久 local/pack/unpack tags 保存本地表项索引，执行时读取共享偏移，不重建 tag 或 MPI 缓冲。几何表在布局改变时重建；同一表禁止跨 stream 并发更新和读取。
- 临时 regrid `ParallelCopyOsi` 继续按旧、新布局各自的显式相位计算；已在 host 按 Fab 预计算的碰撞及通用解码路径保持原有机制。
- 扩展 `cpc_cache_test.cpp`：独立整数取模 oracle、不同尺寸 Fab、连续相位/跳变/uint64 最大值/重置、空 rank、周期 ghost、插值/平均逐值结果、未覆盖哨兵及缓冲复用。

验证结果：

- 改动前后默认 CUDA+MPI 编译通过；最终编译日志为 `logs/compile/compile-20261013T151528.log`，独立测试最终编译为验证目录下 `test_build/logs/compile-20261013T151003.log`。
- 作业 `613064` 的 1/2-rank 独立测试通过，共 28 组布局/相位组合，boundary/interpolation/average/untouched 的 linf 全为 0。同作业的三种插值模式动态 AMR 对照通过（mode 0 为 1/2-rank，mode 1/2 为 2-rank），64 步、三级、regrid_int=32。
- 该作业在额外全周期检查时因脚本固定预期三层而退出：均匀全周期配置实际只产生 level 0，DDF 误差为 0。修正脚本后，作业 `613071` 成功完成全周期单层 64 步、2 MiB host-staging 流水三级 64 步，以及从相同 step-64 checkpoint 恢复至 step 72 的三级对照。
- 七组对照共 19 个层级结果的 valid/active linf=0；6563 条已输出的 OSI/A-B 阶段记录与基线完全一致且 linf=0；44 份平均探针/掩码文件逐字节一致。周期粗细层搬运由独立测试覆盖；全周期生产组不作为多层 AMR 证据。不将这些结果扩展为全部物理 ghost 或长时间数值正确性的证明。
- 2-rank、64 步、mode 0、关闭锁步诊断、2 MiB 流水的三次交替性能测试：按每次两 rank 最大的现有 `perf(s): total` 推进窗口计时，中位数 0.1915530701 s → 0.1884014893 s（下降 1.65%，包含 Stream 更新）；基线范围 0.189590–0.195811 s，改动后 0.185041–0.194880 s。范围重叠，未证明稳定加速；不将窗口时间称为整个进程墙钟时间。
- `git -c core.whitespace=cr-at-eol diff --check` 和验证作业脚本 shell 语法检查通过。尚未验证 GPU-aware MPI、多节点、CPU 完整运行或其他格子模型。

证据与复现：`runs/phase_shifts_validation_20261013/` 保留 before/after 可执行文件及 SHA256、源码/输入快照、独立构建配置、作业脚本、`job613064`/`job613071` 日志和 checkpoint。使用根 `.venv/bin/python` 执行该目录 `analyze.py job613064 job613071` 生成 `analysis.json`。生产数值验证使用冻结的 after.ex；最终源码只比该快照增加了两处生命周期说明注释，并完成再次编译。
