# 同层 ghost 碰撞裁剪实验

目的：检验跳过同层通信可覆盖 ghost 的碰撞是否正确、是否改善真实 AMR 布局的吞吐率。正式 src/config 未修改，不启用该优化。

## 实现与归属

复用 iMultiFab::FillBoundary 标识同层 valid（含周期映射）可覆盖区域；候选只在 OSI 碰撞中跳过 valid 外、同层可覆盖的 ghost，保留粗层插值 ghost 和原 covered/interface 规则。掩码由求解器拥有，在 Collide 按 BoxArray/DistributionMapping 懒重建；不缓存 DDF 或 phase。新增缓存是为了避免每步重新搜索邻块。候选仍发射原矩形区域，因此新增分支/掩码开销，未消除无效线程发射。

实验目录：[ghost_collision_20261023](../../runs/ghost_collision_20261023/)。其中 source.diff/git_head.txt 是实验前现有工作区溯源；candidate/src 是当时完整源码快照，candidate.patch 是相对于该快照的候选改动。包含用户已有修改，但未将其纳入本次提交，也未覆盖正式源码或可执行文件。

复现入口：[测试脚本](../../scripts/temporary/test_ghost_collision.sh)、[审计脚本](../../scripts/temporary/analyze_ghost_collision.py)、[候选补丁](../../scripts/temporary/ghost_collision_candidate.patch)。补丁用 `git apply --unidiff-zero` 应用到实验前源码快照（不是任意历史提交），用根 scripts/compile.sh 在隔离目录编译。GHOST_TEST_MODE 支持 all/control/timing；调度包装器放实验目录。脚本和补丁是本次实验材料，结果需要复现时保留，不是正式生产入口。

## 正确性

- 两次候选编译均成功；最终二进制由完整构建后增量重建获得，各 job 目录记录 SHA256。
- job617898：3D D3Q27，基础 32³，最高 level 2，max_grid_size=16，BF8；三种插值模式 × 非周期/全周期 × 1/2 ranks，共12组，每组64 coarse steps、regrid_int=16，使用已有 regrid_fixture 和 A-B 阶段检查。
- 日志审计：19860 条阶段检查的最大 linf=0；2376 条迁移源检查中的内部 ghost mismatch 全部为0。统计条数不等于格点数，也不表示每个阶段都检查所有 covered 单元。
- 双 rank 非周期的物理域外 source 有 A-B 差异；job617899 关闭优化后三种插值模式的物理 mismatch 计数均与候选相同（各2453208），内部 ghost mismatch=0、阶段最大 linf=0。该物理 ghost 问题独立保留，不能声称所有 source 完全一致。
- 同程序开关对照（job617898/reference 与 candidate）：相同初值、三层 fixture、64步，checkpoint DDF 比较 global linf/l1/l2 均为0。逐层 valid/active 结果见原始日志；不以进程退出码替代逐值证据。

## 性能与边界

job617898 首次真实 checkpoint 动态重网格计时在关闭优化时 OOM（exit6），排除性能统计；当前加密判据与615364历史版本不同，不宣称重现其动态性能。job617897 因提交入口未加执行位未启动，修正后重提。

job617900 在 whshare-agent-66、A100-PCIE-40GB 上，单 rank 使用 GPU0（申请两卡以复用验证资源配置）。同一个程序、同一615364 step256000 checkpoint，每次运行到256320；固定网格 regrid_int=-1，BF32，关闭输出、checkpoint、收敛和 OSI 细分计时。按 off/on/on/off 顺序，两次/模式，每次两个160步窗口；四次应用均正常结束。范围为固定真实布局，不能外推动态重网格或历史615364全程。

| 每160步窗口，四窗口均值 | 关闭 | 开启 | 变化 |
|---|---:|---:|---:|
| perf total (s) | 9.684633 | 9.667586 | -0.176% |
| collide (s) | 6.841140 | 6.660155 | -2.647% |
| MLUPS_total | 876.151 | 877.689 | +0.176% |

窗口 total 范围：关闭9.610808–9.735613 s；开启9.619181–9.721924 s。差异重叠，未发现稳定端到端收益；短窗口碰撞分项也有明显波动，不据此声称稳定内核加速。分层调用次数、launch_cells 应相同（仍发射原区域），审计文件保存完整分层日志。launch_cells 不等于实际执行碰撞的单元数。

结论：这些测试支持在所测条件下省略同层 ghost 重复碰撞，但当前掩码候选没有足够性能证据支持合入。保留实验，生产不改。若继续，应衡量可裁剪格点比例，并比较区域发射方案的额外启动成本。
