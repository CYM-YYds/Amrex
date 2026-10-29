# A 组 128000 步检查点的 grid_eff 对照

## 目的与范围

检验提高聚类效率要求是否能解除 A 组 lev1 整层覆盖、减少相对于原始打标的额外覆盖。
本次复用原实验程序与检查点，只扩展临时实验入口和汇总参数，不修改求解器或默认 inputs。

## 共同设置与复现

- 作业 621485，whshare-agent-185，单张 A100-PCIE-40GB，三组按 0.7、0.85、0.9 顺序串行。
- 共同起点：[A/chk00128000](../../runs/a5_threshold_128000/job621435/A/chk00128000)，物理时间 1000。
- 每组推进 128001–131200，共 3200 步，dt=1/128，终点物理时间 1025。
- 使用原 A 组冻结程序，SHA256 `90ebe0140b8813bda64645988969987c9094cba408b1e77610403dcd190a29d4`，DIM=3、FLOAT、D3Q27。
- Re=1000，A 阈值 0.0625/0.125/0.25/0.5，stream_mode=1，interp_mode=0，基础网格 128³，最高层编号 2。
- max_grid_size=128/256/512，blocking_factor 各方向 16，n_error_buf=1，regrid_int=32。
- 三组共同启用 refinement_diagnostics、重网格 DDF 有限值检查和末态检查，report_int=320；关闭收敛检查，完成固定步数窗口。
- 每组在终点保留速度、Q/涡量 PlotFile 和新检查点。原检查点通过链接读取，不作为输出目录。

实验材料：[runs/temporary/a5_grid_eff_restart/job621485](../../runs/temporary/a5_grid_eff_restart/job621485)。
目录按临时短测归类；输入、冻结程序/源码、原检查点哈希、命令、日志、报告和末态数据保留用于复现。
`compile.log` 来自原 A 组冻结构建，本次没有重新构建或替换数值程序。

复用 `scripts/temporary/a5_threshold_probe.sh`，增加 `PROBE_RESTART_STEP`（默认仍为 3200）
和 `PROBE_GRID_EFFS` 参数，支持同一检查点的效率扫描，不新增平行提交入口。
本次设置 `PROBE_RESTART_ROOT=<case>/runs/a5_threshold_128000/job621435`、
`PROBE_RESTART_CASE=A`、`PROBE_RESTART_STEP=128000`、`PROBE_END_STEP=131200`、
`PROBE_GRID_EFFS='0.7 0.85 0.9'`，末态输出/检查点间隔均为 131200。
每组完整有效参数见对应 `overrides.txt` 和 `command.txt`，提交脚本副本为 `submit.sh`。

汇总入口增加 `--cases` 与 `--threshold-case`，同时输出首次重网格、末次重网格和覆盖范围：

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/analyze_a5_threshold_probe.py \
  projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/a5_grid_eff_restart/job621485 \
  --cases A_eff0p7 A_eff0p85 A_eff0p9 --threshold-case A
```

## 统计口径

覆盖和原始打标比例都以整个计算域为分母；原始标签在父层计数。
实际覆盖来自该细层有效单元数，不含 ghost；各层覆盖重叠，不代表互斥体积分区。
总网格量为三层存储的有效单元数之和，包含被更细层覆盖的粗层单元，不是叶子单元数或按子步加权的更新量。
覆盖范围只统计 100 次重网格后的布局，不包含重启后第一次重网格之前继承的原布局。
末次重网格在 step=131200 推进前执行，日志 time=1024.992188；末态场在该步推进后为 t=1025。

本次使用额外诊断、固定顺序且没有交替重复，只判断网格覆盖和块数，不据此宣称性能提升。
有限值与文件结构检查也不代表新网格已满足全场精度或稳态收敛要求。

## 结果与验证

三组均完成 131200 步并正常退出，调度器状态 SUCCEEDED。有效命令行参数逐项核对，除 grid_eff 外完全一致。

末次重网格结果（比例均相对于整个计算域）：

| grid_eff | 层级 | 原始打标占比 | 实际覆盖占比 | 额外覆盖占比 | 块数 |
|---|---|---:|---:|---:|---:|
| 0.7 | lev1 | 53.3538% | 100.0000% | 46.6462% | 1 |
| 0.7 | lev2 | 22.6771% | 40.2069% | 17.5298% | 153 |
| 0.85 | lev1 | 53.0232% | 81.2012% | 28.1779% | 67 |
| 0.85 | lev2 | 22.4551% | 35.4218% | 12.9667% | 405 |
| 0.9 | lev1 | 52.9578% | 78.8086% | 25.8508% | 104 |
| 0.9 | lev2 | 22.4226% | 34.8999% | 12.4773% | 458 |

整个窗口中各次重网格后的覆盖范围与终点总网格量：

| grid_eff | lev1 覆盖范围 | lev2 覆盖范围 | 终点有效单元总数 |
|---|---:|---:|---:|
| 0.7 | 100.0000%–100.0000% | 39.8682%–40.3839% | 72,839,168 |
| 0.85 | 80.1270%–83.6914% | 35.4004%–36.3159% | 63,262,720 |
| 0.9 | 78.0762%–79.3457% | 34.8999%–35.3760% | 62,160,896 |

三组第一次重网格发生在 step=128032，time=1000.242188，父层原始标签数完全相同：lev0 为 1,118,888，lev1 为 3,804,647。这次几何差异不受先前 AMR 演化分歧影响：

| grid_eff | lev1 首次覆盖 | lev2 首次覆盖 | lev1 / lev2 块数 |
|---|---:|---:|---:|
| 0.7 | 100.0000% | 39.8682% | 1 / 162 |
| 0.85 | 81.3232% | 35.7666% | 66 / 389 |
| 0.9 | 78.6621% | 35.3363% | 111 / 468 |

提高 grid_eff 在本轮能解除 lev1 全覆盖。相对 0.7，0.85/0.9 终点有效单元总数分别减少约 13.15%/14.66%；0.9 相对 0.85 再减少约 1.74%，但两层块数进一步增加。不能把网格量下降直接等同于运行加速或精度达标。

验证：

- 每组 100 次重网格、300 次 DDF 有限值检查全部通过；200 次阈值计数交叉检查一致，涡量诊断非有限计数为 0。
- rank_exit_0.json 与 guard 均为 exit_code=0，completion_seen=true；新检查点均为 step=131200、t=1025、最高层编号 2。
- 三组共 6 份速度/涡量 PlotFile 通过 Header、Cell_H、FAB 文件及完整数据长度检查（plotfiles_layout_validation.json）；本轮未额外执行 ParaView 读取或全场精度比较。
- 原始检查点 8 个文件的 SHA256 复核全部通过；旧 A/B 数据未修改。
- Shell bash -n、Python AST 语法检查、git diff --check 通过；不涉及构建或数值内核修改。

保留 summary.json、coverage.csv、plotfiles_layout_validation.json、analysis_source.py、analysis_command.txt 与三组原始输出，供后续精度和布局分析。

