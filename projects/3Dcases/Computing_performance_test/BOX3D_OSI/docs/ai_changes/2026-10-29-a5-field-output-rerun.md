# A/B 阈值重跑并保留速度、涡量输出（2026-10-29）

## 目的与范围

用户要求恢复 A/B 的场结果，用于后续 ParaView 检查流场、涡量与准确性。检查时原来的 A/B 目录、冻结程序、输入及 checkpoint 头仍在；此前诊断设置 `amr.plot_int=-1`，本来没有保存这类场输出。本次独立从静止重跑 A、B 到 step12800、t=100，不覆盖旧结果。

A/B 指两组 **AMR 涡量阈值**，两组运行模式都是 OSI，不是 canonical A-B 与 OSI 的性能对照。

- A：`lbm.err=0.0625 0.125 0.25 0.5`。
- B：`lbm.err=0.125 0.25 0.5 1`。
- 与初测相同：FLOAT、D3Q27、BGK、Re=1000、L=1、U_lid=0.05、128³、最高 level 2，BF16、max_grid_size=128、n_error_buf=1、grid_eff=0.7，每 32 个粗层步重构。
- 使用 job621293 冻结程序、输入与源码，不重新编译。新旧程序与输入均经 cmp 确认一致；程序 SHA256 为 `b461f1d84169513512055c2fac204072e2dee19ee824208a58f7c3f5e1c64996`。
- job 621375，A/B 在同一节点 whshare-agent-73、单 rank/单 A100-PCIE-40GB 上依次运行。开启诊断和输出，计时不用于性能判优。

## 主要改动

扩展既有临时扫描入口，参数化从静止使用冻结快照、选择 A/B、输出间隔与结果根目录（PROBE_SNAPSHOT_ROOT、PROBE_CASES、PROBE_PLOT_INT、PROBE_OUTPUT_ROOT）。保留原初测及续跑默认行为，不复制新提交入口。未修改用户主 inputs、求解器或 MLUPS 记录。

增加临时 PlotFile 复核入口：现有中心线导出工具针对速度采样，本次还需要原生涡量数组和 Header/Cell_H/FabOnDisk 完整性检查，因此使用独立的任务脚本，未改公共 ParaView 工具。脚本作为本批结果复现入口保留。

## 长期保留路径与内容

[结果根目录](../../runs/a5_threshold_fields/job621375) 为用户要求长期保留的准确性复核材料，**不按 temporary 实验自动清理**。保留程序/输入/源码/配置快照、原编译日志、指纹、命令、退出记录、场文件和终态 checkpoint。

每组 A/、B/ 内均保存：

| 模拟时间 | 粗层步数 | 速度目录（ux/uy/uz） | 涡量目录（Q/Vorticity） |
| --- | --- | --- | --- |
| 25 | 3200 | plt_003200 | plt_vort_003200 |
| 50 | 6400 | plt_006400 | plt_vort_006400 |
| 75 | 9600 | plt_009600 | plt_vort_009600 |
| 100 | 12800 | plt_012800 | plt_vort_012800 |

共 16 份 PlotFile；各组另有 chk00012800，Header 记录 step12800、time100、finest_level2。checkpoint.keep_latest_only=false；场文件未做清理。

`Vorticity` 是求解器原生输出的涡量模，`Q` 为另一独立数组。两类数据分别为不同 PlotFile，具有相同时间、几何范围及层级。使用原生涡量即可查看，不需要先在 ParaView 重算 Gradient。

[读取说明](../../runs/a5_threshold_fields/job621375/PARAVIEW_README.md) 和 [字段索引 CSV](../../runs/a5_threshold_fields/job621375/fields_index.csv) 列出完整路径。AMReXBoxLibGridReader 的 FileNames 使用 PlotFile 目录，与仓库既有中心线工具一致；不能在该参数中传目录末尾的 /Header。

## 验证

- 调度状态 SUCCEEDED；A/B 的 MPI、guard 退出码均为 0；正常 finalized。
- 每组完成 400 次运行时重构，1199 次逐层有效区 DDF NaN/Inf 检查通过，801 次输入阈值 tag/CDF 计数一致；涡量诊断 nonfinite=0。终态三个层级的 DDF/宏观量检查均通过，密度为正。
- 各组四个时刻的 16 份文件，均检查变量名、三维、物理时间、域 [0,1]³、最高 level2、各层 Cell_H、FAB 个数、数据文件/偏移和每块完整字节长度。确认实际场数据为 4 bytes。
- 16 份文件均在 whshare-agent-1 用私有 ParaView 6.2 的 pvbatch 实际读取三层，核对边界、每层非空以及 ux/uy/uz 或 Q/Vorticity 全部已读值的有限性。未操作用户当前 GUI。
- 用真实 FAB 头构造缺少主体数据的临时样本，确认校验器拒绝截断文件，原始数据未修改。Shell bash -n、Python AST 语法解析、报告链接与 git diff --check 通过。

终态网格与此前诊断的同组结果一致：

| 组别 | level 1 全域覆盖 | level 2 全域覆盖 |
| --- | --- | --- |
| A | 68.75% | 30.23% |
| B | 36.43% | 15.38% |

覆盖率包含被更细层覆盖的 valid cells，不是 active 单元比例。相同覆盖不代表 DDF 逐值相同；本轮未做逐值重启等价或 A/B 准确性判定。t=100 只有 5 个对流时间单位，不代表稳态，也未达到论文 A5 的 t_f=1000。

## 复现

在算例根目录提交；也可把 PROBE_SNAPSHOT_ROOT 换成本次长期保留目录，它已包含独立的冻结程序、输入和源码：

```bash
CCS_CLI_HOME=/opt/batch/cli dsub -x 'all;PROBE_SNAPSHOT_ROOT=/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/a5_threshold_probe/job621293;PROBE_CASES=A B;PROBE_END_STEP=12800;PROBE_PLOT_INT=3200;PROBE_OUTPUT_ROOT=/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/a5_threshold_fields;' -s scripts/temporary/a5_threshold_probe.sh
```

在仓库根目录检查布局：

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/validate_a5_plotfiles.py projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/a5_threshold_fields/job621375
```

ParaView 复核按 [paraview 技能](../../../../../../.agents/skills/paraview/SKILL.md) 的 batch 方式在 whshare-agent-1 执行同一脚本并加 --read。不要在登录节点运行 pvbatch。

证据：[布局检查](../../runs/a5_threshold_fields/job621375/plotfiles_layout_validation.json)、[ParaView 检查](../../runs/a5_threshold_fields/job621375/plotfiles_reader_validation.json)、[读取日志](../../runs/a5_threshold_fields/job621375/paraview_validation.log)、[运行汇总](../../runs/a5_threshold_fields/job621375/summary.json)。
