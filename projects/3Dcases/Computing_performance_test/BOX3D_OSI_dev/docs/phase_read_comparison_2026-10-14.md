# OSI 本地通信 phase 表读取单变量对照

2026-10-14，作业 613430。测试仅替换 FillBoundaryOsi 本地 copy kernel 的两处
phase_shifts[tag.*_phase_index] 读取为 osi_phase_shift(phase, tag.*_fab)。
保持 phase 表更新、tag、Q、启动方式、同步、MPI 分支及其他阶段不变。

同一 GPU 顺序运行：缓存、现场计算、现场计算、缓存，每次从头运行 3200 步。
输入：128³、Re=1000、level 0–2、regrid_int=32、关闭 plot 和提前收敛停止。
performance.report_int=3200；终态 checkpoint 对照在计时窗口之后执行。

| 顺序 | 方式 | osi_fillboundary (s) | comm (s) | total (s) |
|---|---|---:|---:|---:|
| 1 | 缓存 | 9.307604 | 9.329820 | 86.244539 |
| 2 | 现场计算 | 9.317628 | 9.339860 | 86.064242 |
| 3 | 现场计算 | 9.325081 | 9.347380 | 86.116039 |
| 4 | 缓存 | 9.326950 | 9.349021 | 86.173219 |

缓存版本地通信均值 9.317277 s，现场计算版 9.321355 s，现场计算高 0.0438%。
缓存版两轮之间相差约 0.208%，现场计算两轮相差约 0.080%。没有观察到恢复现场
计算的收益，无法把此前 604961/613417 的约 3.21% 通信增长归因于 phase 表读取。
本结论只覆盖当前单 GPU、最初 3200 步窗口，不能排除其他网格布局下的不同表现。

后三轮相对第一轮的各层 active DDF 对照均为零差，global linf/l1/l2 均为 0。
不将 active 零差解释为所有 covered 数据均有限。四轮均正常 finalized。

证据：[测试脚本](../runs/phase_read_comparison_20261014/submit.sh)、
[缓存基线](../runs/phase_read_comparison_20261014/run1_cached/run.log)、
[现场计算一](../runs/phase_read_comparison_20261014/run2_computed/run.log)、
[现场计算二](../runs/phase_read_comparison_20261014/run3_computed/run.log)、
[缓存重复](../runs/phase_read_comparison_20261014/run4_cached/run.log)。

临时测试源码已恢复，生产可执行文件恢复为缓存版；未增加永久配置或替代路径。

cached.ex SHA256：`745149e807a4feb54295978f56e4e8c881040f03e6fea087ffa5c8c77638e055`。

computed.ex SHA256：`954ffcb9c3600921f05967302c67974a56360be188d5e7fdb7fcd4434e2a402b`。

inputs SHA256：`a23a8f9750899ac6e1f28332c8f060a05423f970128413f920e414a39ffcf84a`。
