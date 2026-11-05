# 修订累加优化：OSI最后，1000步

原始材料：/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/optimization_ladder/round4_osi_last_1000/job626501。节点：whshare-agent-186。

每组从624717/C checkpoint128000恢复到129000，单A100/单rank、FLOAT/D3Q27、Re1000、C阈值；动态AMR每32步重构。各组仅一次1000步，十个100步窗口。稳定场出发不代表正式收敛，也不消除短测噪声。

基线为A-B，关闭平均与插值批量、边界批量薄面，碰撞256线程。先累加平均插值、边界、碰撞128线程，最后切OSI。坐标复用只对OSI环形地址有效，最后一项合并OSI实现与坐标复用，不能把该增益全部归因于纯OSI切换。通信隐藏在单rank回退，本图不单列；碰撞批量仍作为历史额外实验，不采用。

线程开关补齐A-B路径，掩码、计算公式和浮点运算顺序不变。沿用job626450冻结源码及数值修复，不混入工作区其他修改，不代表某一完整历史提交。平均两种路径共用同一归约和GPU任务表，只改变启动粒度。

平均MLUPS=十窗口有效更新量之和/累计perf.total秒数/1e6，分子由打印MLUPS_total×total恢复；含重网格，排除初始化、恢复、输出和窗口报告。

| 累加项 | MLUPS_total | 相邻变化 | 相对基线 | total/s | 碰撞工作量与基线一致 |
|---|---:|---:|---:|---:|---|
| A-B 基线 | 1132.07 | +0.00% | 1.000x | 47.979 | True |
| + 平均与插值批量 | 1500.12 | +32.51% | 1.325x | 36.208 | True |
| + 边界批量薄面、直接构面 | 1583.12 | +5.53% | 1.398x | 34.309 | True |
| + 碰撞256→128线程 | 1673.21 | +5.69% | 1.478x | 32.462 | True |
| + OSI（含坐标复用） | 2429.80 | +45.22% | 2.146x | 22.354 | True |

数值验证：每组另做64步动态AMR恢复，各组在64步和完整1000步后比较所有valid（含covered）及active DDF，结果见validation.json及各组run.log。A-B交换缓冲ghost存在残留差异，未宣称ghost全等；首轮全文件SHA失败证据另存job626485/checkpoint_byte_difference.json；同阶段完整valid比较见该目录same_stage_valid_comparison.json。checkpoint保存前在计时外统一执行完整平均和有限值检查，末尾不重复ComputeMacro；A-B与OSI的所有valid比较均在完整平均后进行。工程检查不等于长期物理等价或收敛证明。

100步窗口不是独立重复，小幅差异不能证明稳定加速；不能把前轮5000步结果与本作业1000步结果直接作差归因。

[主图PNG](optimization_ladder.png) · [PDF](optimization_ladder.pdf) · [逐窗口CSV](windows.csv)

复现绘图（仓库根）：

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/optimization_ladder/analyze.py /home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/optimization_ladder/round4_osi_last_1000/job626501 --osi-last --output-dir /home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/data_post_processing/results/optimization_cumulative_OSI_last_C128000_1000
```
