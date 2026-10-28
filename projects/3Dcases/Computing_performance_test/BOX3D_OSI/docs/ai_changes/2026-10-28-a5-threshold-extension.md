# A5 阈值 A 组延长至 t=100（2026-10-28）

目的：接续第一轮阈值 A，观察早期之后的网格覆盖与有限值。job 621295 正常完成；本阶段仍为临时诊断，不作为 A5 性能验收。

## 范围与复用

沿用 [第一轮初测](2026-10-28-a5-threshold-probe.md) 的冻结程序、输入、源码和 step3200 checkpoint，从 t=25 接续至 step12800、t=100。场精度 FLOAT、Re=1000、L=1、U=0.05、128³、最高 level 2、BF16、max_grid_size=128、n_error_buf=1、grid_eff=0.7、每 32 个粗层步重构均保持不变；阈值仍为 `0.0625 0.125 0.25 0.5`。

本次未修改求解器或主 inputs，也未重新构建程序；新旧程序及输入均经 cmp 确认逐字节一致，程序 SHA256 为 `b461f1d84169513512055c2fac204072e2dee19ee824208a58f7c3f5e1c64996`。执行节点为 whshare-agent-185，单 rank、单 A100-PCIE-40GB。节点与前轮不同，本轮不作计时优劣比较。

扩展现有临时提交脚本，新增 `PROBE_RESTART_ROOT` 与 `PROBE_END_STEP`，避免复制按作业号命名的新入口；无参数时仍运行原 A/B/C 初测。重启源 checkpoint 仅作为读取链接；新 checkpoint 写入独立作业目录。分析脚本改为按 Header 中步数识别最新 checkpoint，兼容初测与续跑。

## 覆盖演化

覆盖率为各层 valid cells / 该层全域单元数，包含被更细层覆盖的单元，不是 active 单元比例。t=25 来自第一轮终态，其余来自本轮；对应布局分别由终点前 Δt₀=1/128 的重构产生。

| 模拟时间 | 累计粗层步数 | level 1 全域覆盖 | level 2 全域覆盖 |
| --- | --- | --- | --- |
| 25 | 3200 | 23.51% | 15.11% |
| 50 | 6400 | 31.62% | 20.63% |
| 75 | 9600 | 38.89% | 29.09% |
| 100 | 12800 | 68.75% | 30.23% |

本续跑段记录的最大覆盖为 level 1 **68.75%**、level 2 **30.83%**，均未达到全域。终态 level 1 有 8 个盒、11,534,336 个 valid cells；level 2 有 110 个盒、40,570,880 个 valid cells。

后段存在显著的布局跳变：

| 重构时间 | level 0 原始 tag 比例 | level 1 最终全域覆盖 | level 1 盒数 |
| --- | --- | --- | --- |
| 98.9921875 | 32.12% | 48.02% | 25 |
| 99.2421875 | 32.19% | 68.75% | 8 |

原始超限比例只小幅增长，最终覆盖却从约 48% 跳到 68.75%，同时盒数下降。因此覆盖增长不能全部解释成同幅度的涡量超限增加，需要区分 raw tags 与经过缓冲、嵌套和聚类约束后的最终盒布局。本次没有独立扫描聚类参数，尚未把跳变归因到某一个 AMReX 参数。

## 验证结果

- 调度终态 SUCCEEDED；MPI 启动器与 guard 退出码均为 0，未触发 1800 秒保护上限。
- checkpoint Header 确认 step=12800、time=100、finest_level=2；实际从 step3200 开始接续，而非重新从零运行。
- 300 次重构、900 次逐层有效区 DDF NaN/Inf 检查全部通过；涡量诊断 nonfinite=0。
- 600 次输入阈值对应的 tag/CDF 计数全部一致；终态三个层级的 DDF 和宏观量检查通过，密度为正。
- Shell bash -n、Python AST 解析通过；修改后的分析入口重读第一轮 A/B/C，终态 checkpoint 和检查结果保持一致。报告链接及 git diff --check 通过。
- 本次未做连续运行/重启逐值等价对照、中心线精度或稳态收敛验证。诊断与有效区检查影响计时，未把本轮 MLUPS 填入性能表3。

## 当前判断

A 组在 t=100 前仍保持局部加密，但 level 1 覆盖已明显增长且出现布局跳变，不能据 t=25 的结果就确定它适用于 t=1000 正式对标。本次不修改用户主输入，也不将该阈值确认为最终方案。

下一轮更有价值的是将 B 组（0.125 / 0.25）也延长至同一 t=100，保持网格参数不变，比较覆盖演化与后段跳变，再决定是否需要单独试验聚类参数。该比较尚未执行；更高阈值的数值精度也不能仅凭覆盖率确认。

## 复现与保留

在算例根目录提交（需要调度客户端环境）：

```bash
CCS_CLI_HOME=/opt/batch/cli dsub -x 'all;PROBE_RESTART_ROOT=/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/a5_threshold_probe/job621293;PROBE_END_STEP=12800;' -s scripts/temporary/a5_threshold_probe.sh
```

从仓库根目录重新分析：

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/analyze_a5_threshold_probe.py projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/a5_threshold_probe/job621295
```

[作业目录](../../runs/temporary/a5_threshold_probe/job621295) 保存 frozen 程序/输入/源码/配置、指纹、原程序来源 application_git_head.txt/application_source.diff、本次提交时工作树差异、stage.txt、提交脚本、GPU/hostfile、逐组 command.txt/run.log/exit_status.json，以及终态 chk00012800。原 chk00003200 和前轮结果均保留。

[汇总](../../runs/temporary/a5_threshold_probe/job621295/summary.json)、[覆盖序列](../../runs/temporary/a5_threshold_probe/job621295/coverage.csv)、[分析输出](../../runs/temporary/a5_threshold_probe/job621295/analysis.log) 保留；临时脚本仍作为本报告复现入口，无运行数据删除。
