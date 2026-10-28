# A5 阈值 B 组延长至 t=100 及 A/B 覆盖对照（2026-10-28）

目的：在 [A 组后段出现较大覆盖扩张](2026-10-28-a5-threshold-extension.md) 后，将 B 组也推进至同一模拟时间，比较覆盖演化。job 621305 正常完成；本轮是阈值诊断，不是性能或数值精度验收。

## 运行范围

B 组从 [第一轮初测](2026-10-28-a5-threshold-probe.md) 自己的 step3200、t=25 checkpoint 接续至 step12800、t=100；没有将 A 的场切换为 B 阈值。阈值仍为 `lbm.err = 0.125 0.25 0.5 1`，最高 level 2 下真正控制两次加密的是前两项。

冻结程序、输入、源码与首轮一致，程序和输入均经 cmp 核对逐字节相同。程序 SHA256 为 `b461f1d84169513512055c2fac204072e2dee19ee824208a58f7c3f5e1c64996`。未重新构建，也未修改用户主 inputs、求解器或 MLUPS 表。

运行条件与 A 延长阶段一致：FLOAT、D3Q27、BGK、OSI、线性插值、Re=1000、L=1、U=0.05、128³、最高 level 2、BF16、各层 max_grid_size=128、n_error_buf=1、grid_eff=0.7，每 32 个粗层步重构。单 rank/单 A100-PCIE-40GB，节点 whshare-agent-73，与 A 的执行节点不同，不作跨节点计时优劣比较。关闭提前收敛停止与 PlotFile，开启重构诊断和有效区有限值检查。

## 覆盖结果

覆盖率 = 该层 valid cells / 该层全域单元数，包含被更细层覆盖的单元。表中端点对应前一个粗层时间步的重构布局，例如 t=100 使用 t=99.9921875 生成的网格。

| 模拟时间 | 累计步数 | A level 1 | B level 1 | A level 2 | B level 2 |
| --- | --- | --- | --- | --- | --- |
| 25 | 3200 | 23.51% | 21.88% | 15.11% | 12.30% |
| 50 | 6400 | 31.62% | 23.24% | 20.63% | 15.90% |
| 75 | 9600 | 38.89% | 32.42% | 29.09% | 15.29% |
| 100 | 12800 | 68.75% | 36.43% | 30.23% | 15.38% |

| 本续跑段（t=25→100） | A level 1 | B level 1 | A level 2 | B level 2 |
| --- | --- | --- | --- | --- |
| 最大覆盖率 | 68.75% | 37.50% | 30.83% | 20.50% |

B 的最高覆盖未出现 A 后段 68.75% 的扩张。终态 B 的 level 1 有 22 个盒、6,111,232 个 valid cells；level 2 有 61 个盒、20,647,936 个 valid cells。A 对应为 8 个盒/11,534,336 cells 和 110 个盒/40,570,880 cells。覆盖/单元数与盒数要分开看，不能仅以覆盖减少推断实际加速。

两组从静止开始各自按其阈值演化，网格和流场均可能不同；这不是在同一份涡量场上只过滤标签的静态对照。本次未比较中心线、边界层或逐值 DDF，不把较低覆盖当作精度充分的证据。

## 验证

- 调度状态 SUCCEEDED；MPI 和 guard 退出码均为 0，AMReX 正常 finalized。
- 终态 Header 确认 step=12800、time=100、finest_level=2；从 B 的 chk00003200 接续，原 checkpoint 未覆盖。
- 300 次重构、900 次逐层有效区 DDF NaN/Inf 检查通过；涡量诊断 nonfinite=0。
- 600 次输入阈值的 tag/CDF 计数核对均一致；终态三个层级的 DDF 和宏观量检查通过，密度为正。
- 本次未验证稳态、中心线精度、连续运行/重启逐值等价，未把诊断计时填入性能表3。

## 工具调整及检查

复用既有扫描脚本，新增 `PROBE_RESTART_CASE=A|B|C` 选择对应的首轮 checkpoint、rank 包装器与阈值；默认 A 保持旧调用兼容。不复制 B 专用入口。

运行中曾因日志末尾半行被解析为完整 CDF 记录，产生一次监控计数临时误报；完整日志重读没有实际计数不一致。分析脚本现仅解析换行结束的完整记录，避免将尚未写完的数字当作失败。用本轮真实 CDF 记录在 ge_cells 数字中间截断复现并检查修正；完整初测 A/B/C 及 A 延长阶段重读后，汇总 JSON 与修改前完全一致。

Shell bash -n、Python 语法解析、实际 B 续跑、截断日志检查及完整历史日志兼容检查通过；报告链接与 git diff --check 通过。

## 建议

从 t=100 前的覆盖控制看，优先把 B 而非 A 作为下一阶段候选，暂时没有依据继续盲目提高到 0.64 / 1.28。仍需延长时间并做速度剖面等数值对照；本次 t=100 只相当于 5 个对流时间单位，不能保证 t=1000 时的覆盖或精度。

A、B 最细分辨率均为 512³，但与论文 A5 的根网格、层数、覆盖范围和更新量不同；后续正式性能对标需记录实际工作量与计时范围，不能单凭 MLUPS 判优。本次不自动改写用户主输入。

## 复现与保留

在算例根目录提交：

```bash
CCS_CLI_HOME=/opt/batch/cli dsub -x 'all;PROBE_RESTART_ROOT=/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/a5_threshold_probe/job621293;PROBE_RESTART_CASE=B;PROBE_END_STEP=12800;' -s scripts/temporary/a5_threshold_probe.sh
```

从仓库根重新分析：

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/analyze_a5_threshold_probe.py projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/a5_threshold_probe/job621305
```

[作业目录](../../runs/temporary/a5_threshold_probe/job621305) 保留冻结程序/输入/源码/配置、源程序来源、指纹、stage.txt、GPU/hostfile、提交与分析脚本、命令、日志、退出记录、chk00012800；旧 chk00003200 为只读用途链接。[汇总](../../runs/temporary/a5_threshold_probe/job621305/summary.json)、[覆盖序列](../../runs/temporary/a5_threshold_probe/job621305/coverage.csv)、[分析输出](../../runs/temporary/a5_threshold_probe/job621305/analysis.log) 均保留，未删除前轮或本轮运行数据。临时脚本作为复现入口继续保留。
