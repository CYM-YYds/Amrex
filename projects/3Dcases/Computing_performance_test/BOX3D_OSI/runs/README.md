# 计算结果入口

整理日期：2026-11-03。目录存在不代表已经完成或通过数值验收；每组均按各自冻结程序与输入解读。

## 近期阈值、续跑与显存实验

| 用途 | 入口 | 已核实状态 |
|---|---|---|
| C从静止到t1000、GIF流场 | [a5_C_t1000_gif](a5_C_t1000_gif/README.md) | 624717 SUCCEEDED/exit0，step128000/t1000，速度和Q/涡量各40份；末次converged=0 |
| B续跑到t2000 | [a5_B_continue_t2000](a5_B_continue_t2000/analysis/report.md) | 623697 SUCCEEDED/exit0，step256000/t2000，末次正式相对变化2.571148216e-4，未收敛 |
| C/D从B检查点短测 | [C](temporary/C_from_B_compression/report.md)、[D及流场索引](temporary/D_from_B_compression/report.md) | 623707/623748均成功至step128128/t1001，仅短期网格规模证据 |
| C双数组A-B/OSI性能与显存 | [原对比](C_AB_OSI_t1000_peak_memory/README.md)、[A-B重试](C_AB_t1000_peak_memory_rerun/README.md) | 624739、624780/624781、624810/624811全部FAILED/exit6，VisMF写失败；624810到step6400，其余step3200，无完整全程性能结论 |
| 均匀512³恢复成功终态 | [源结果](uniform512_local_staging_t1000/)、[来源核验](../data_post_processing/results/C_uniform512_Cortes/README.md) | 623012恢复分段计算completed，step512000/t1000终态用于中心线；与下方失败的622116区分 |

曲线和动画请查[后处理README](../data_post_processing/README.md)。具体命令、程序指纹及原始失败材料保留，I/O根因仍pending，不自动重新提交。

## 此前优化计算与兜底结果

下面优化生产计算使用各自冻结程序，不能替代当前HEAD的长程验收。

| 用途 | 入口 | 本次整理时核实的状态 |
|---|---|---|
| 优化均匀网格 | [uniform512_t1000](uniform512_t1000/README.md) | 512³、Q27、目标 t=1000；622116 推进至 t=1000，最终 VisMF 场输出失败（FAILED/exit 6），没有完整终点场 |
| 优化自适应网格 | [a5_B_optimized_128000](a5_B_optimized_128000/README.md) | 622137/B 完成 128000 步、t=1000，SUCCEEDED/exit 0，converged=0；622026、622111 为此前写盘失败尝试 |
| 完整均匀网格兜底 | [256³、t=1000](archive/history_20261101/uniform_256_lev0_t1000/README.md) | 621699 完成；场数据通过其中 job621699 入口访问 |
| 完整自适应网格兜底 | [旧 A/B 长程](archive/history_20261101/a5_threshold_128000/README.md) | 621436/B 完成 t=1000；旧程序结果，不标为最新优化结果 |

历史生产计算与快照集中在 [archive/history_20261101](archive/history_20261101/)。旧顶层名字是兼容软链，供已有文档、重启命令和后处理使用，不是重复数据；请勿按软链路径删除内容。

[temporary](temporary/) 保留原位：其中包含已被文档引用的性能对照、数值回归、冻结程序和复现材料，不按目录名自动判废。已有 archive 内容也保持不变。本轮未压缩、未删除数据，不释放存储空间。

新结果完成且验收前保留完整兜底。日后若需要实际释放空间，应另按场输出、checkpoint、日志和复现依赖列出删除范围。

归档清单与验证见 [整理记录](../docs/ai_changes/2026-11-01-runs-archive.md)。
