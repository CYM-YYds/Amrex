# A5 对标前的涡量阈值初测（2026-10-28）

目的：在 128³、最高 level 2 的 AMReX 块结构中，检查三组加密阈值的早期覆盖，筛查整层加密及非有限值。临时实验 job 621293；本报告不作为收敛、数值精度或性能验收。

## 影响范围与实际入口

本次未修改求解器、主 inputs 或 MLUPS 表。使用用户已经修改的工作树：Re=1000，三维 `ErrorEst()` 调用 `state_error_3`，直接用 `vorticity[...,1] >= err[lev]` 打标。用户已有源文件格式修改与文档改动均保留，未纳入本次提交。输入中的“三维不读取 lbm.err”注释已与该工作树不符；本实验以冻结源码和实际 tag/CDF 计数为准。

新增两份临时脚本，复用现有 compile.sh、run_rank.sh、guard_run.py、refinement_diagnostics、regrid_valid_check 和 final_state_check。独立扫描入口仅负责同一 GPU 分配内的三组输入覆盖与材料冻结，不改变正式提交机制；分析入口负责解析现有诊断并核对 tag/CDF，一并保留用于本次报告复现。

## 冻结条件

- 单 rank、单 NVIDIA A100-PCIE-40GB，节点 whshare-agent-82；FLOAT 场精度（日志确认 4 bytes），D3Q27、BGK、OSI、线性插值。
- L=1、U=0.05、Re=1000，128³ 基础网格，最高 level 2，最细等效 512³。
- BF16，各层 max_grid_size=128，n_error_buf=1，grid_eff=0.7，每 32 个粗层步重构。
- 每组从静止独立开始，推进 3200 个粗层步至 t=25，即 1.25 个对流时间；关闭提前收敛停止与 PlotFile，终态 checkpoint 保留。
- 开启详细涡量归约、逐重构有效区 DDF NaN/Inf 检查与终态 DDF/宏观量检查；本轮计时不用于性能判优，也未填入性能表3。

## 结果

下表覆盖率均为该层 valid cells 除以该层全域单元数，包含被更细层覆盖的单元，不是 active 更新单元比例。终态布局由最后一次重构（t=24.9921875）生成，保存于 t=25 checkpoint。

| 组别 | level 0→1 阈值 | level 1→2 阈值 | level 1 全域覆盖 | level 2 全域覆盖 | level 1 / 2 盒数 | 终态与有限值 |
| --- | --- | --- | --- | --- | --- | --- |
| A | 0.0625 | 0.125 | 23.51% | 15.11% | 19 / 37 | 3200 步、三层、通过 |
| B | 0.125 | 0.25 | 21.88% | 12.30% | 6 / 20 | 3200 步、三层、通过 |
| C | 0.25 | 0.5 | 14.84% | 8.07% | 6 / 30 | 3200 步、三层、通过 |

三组均正常退出；各有 299 次逐层 DDF 有限值检查通过，全部涡量诊断的 nonfinite=0；终态三个层级的检查均通过，密度为正。每组 201 次 tag 计数与相同阶段、相同层级、相同阈值的 CDF 超限计数完全一致，确认 inputs 阈值实际生效。终态 Header 均记录 step=3200、time=25、finest_level=2。

整个已记录的重构过程均未出现 level 1/2 全域覆盖。最大覆盖率如下，不仅检查终态：

| 组别 | level 1 最大覆盖 | level 2 最大覆盖 | 终态 level 0 原始 tag 比例 | 终态 level 1 原始 tag 比例 |
| --- | --- | --- | --- | --- |
| A | 30.18% | 15.40% | 13.76% | 45.41% |
| B | 31.25% | 14.84% | 10.86% | 38.03% |
| C | 25.00% | 9.38% | 8.38% | 35.58% |

原始 tag 比例以 ErrorEst 当时的该层 valid cells 为分母；这是聚类前旧布局的观测，不能直接与重构后该层覆盖率相减解释为多余加密。不同阈值会改变后续流场、网格与聚类结果，覆盖率随阈值变化不必在每一个瞬间严格单调。

## 判断与下一步

暂时保留 A 的 `lbm.err = 0.0625 0.125 0.25 0.5` 作为继续观察的起点：在本次早期阶段，它已避免整层加密，没有证据要求立即增大到 0.64 / 1.28。B、C 可以降低终态覆盖，但尚未比较中心线速度、边界层和时间收敛，不能据此认为精度足够或性能更优。

该判断仅覆盖从静止到 t=25；不能保证 t=100、t=1000 的覆盖仍局部。下一阶段可先延长 A 并观察覆盖演化，再做 t=1000 的固定时长对标。A5 与本配置最细分辨率相同，但基础网格、层数、覆盖范围和更新量不同，后续需同时报告网格工作量，不能只凭 MLUPS 宣称相同任务加速。

## 验证与保留材料

- 编译：`PRECISION=FLOAT GEN_CCDB=0 ./scripts/compile.sh` 成功；使用当前用户三维阈值入口。
- 脚本：Shell `bash -n`、Python AST 解析及 `git diff --check` 通过。
- 提交：在算例根目录执行 `dsub -s scripts/temporary/a5_threshold_probe.sh`，job 621293；A/B/C 的 status.tsv 与 MPI/guard 退出码均为 0。
- [实验目录](../../runs/temporary/a5_threshold_probe/job621293)：输入、源码/配置快照、source.diff、程序及 SHA256、compile.log、GPU/节点、逐组 command.txt/run.log/exit_status.json 和终态 checkpoint 均保留。
- [汇总数据](../../runs/temporary/a5_threshold_probe/job621293/summary.json)、[覆盖序列](../../runs/temporary/a5_threshold_probe/job621293/coverage.csv)、[分析输出](../../runs/temporary/a5_threshold_probe/job621293/analysis.log)。
- [提交脚本](../../scripts/temporary/a5_threshold_probe.sh)、[分析脚本](../../scripts/temporary/analyze_a5_threshold_probe.py)。重新汇总命令（仓库根目录）：

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/analyze_a5_threshold_probe.py projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/a5_threshold_probe/job621293
```

临时属性与保留范围见实验上级 README。脚本为本报告复现入口，暂时保留；未删除运行数据。
