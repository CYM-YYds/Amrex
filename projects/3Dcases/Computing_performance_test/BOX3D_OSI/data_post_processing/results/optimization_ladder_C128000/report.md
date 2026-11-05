# 单 GPU 优化累加实验

来源：/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/optimization_ladder/round1/job626333。冻结程序 SHA256 见 executable.sha256；源码及差异、每次实际命令、日志、退出状态均保留。

每配置从 624717/C step128000/t1000 恢复并推进5000粗步，单 rank/单 A100、FLOAT、D3Q27、Re1000、C阈值。动态AMR每32步重构，关闭输出和收敛检查，所有配置相同。

平均 MLUPS = 10个500步窗口有效更新量之和 / 推进 total 耗时之和 / 1e6。日志未输出独立更新总数，分子从各窗口已打印的 MLUPS_total × total 恢复，受打印精度影响；不是最后窗口或启动器整次耗时。

| 累加项 | 平均 MLUPS | 相邻变化 | 相对基线 | total/s | 与OSI逐窗口碰撞工作量一致 |
|---|---:|---:|---:|---:|---|
| A-B 基线 | 1214.52 | +0.00% | 1.000x | 217.492 | True |
| + OSI 实现 | 1376.64 | +13.35% | 1.133x | 191.880 | True |
| + 插值批量提交 | 1792.76 | +30.23% | 1.476x | 147.342 | True |
| + 边界批量薄面 / 直接构面 | 1953.80 | +8.98% | 1.609x | 135.198 | True |
| + 通信隐藏（单卡回退） | 1954.47 | +0.03% | 1.609x | 135.152 | True |
| + 碰撞线程 256 → 128 | 2132.35 | +9.10% | 1.756x | 123.877 | True |
| + 碰撞坐标复用（现役策略） | 2409.03 | +12.98% | 1.984x | 109.650 | True |

基线为当前源码中受控撤回指定优化的版本，保留所有数值修复和未列出的优化，不等于某个完整历史提交。边界撤回采用每面厚度nghost+1的候选区并逐标签发射，仍覆盖修复后的完整物理面；插值复用同一缓存/任务区域，仅切换逐任务和批量启动。

现役正式碰撞仍逐Fab提交；碰撞批量实验曾退化，不能将本表插值收益标为碰撞批量收益。线程数和坐标复用分别列出。单rank通信隐藏回退，不把这一行的波动归因为通信收益。

各配置只有一次5000步测量，500步窗口不是独立重复；相邻小差异不能证明稳定加速。64步动态AMR优化回归见validation.json；本次A-B对OSI的最终DDF比较valid/active全0，详见check_00_AB/run.log。它不证明长期数值等价或收敛。

job626333全部14次应用退出0；最初汇总对guard completion_seen的判断过严，作业末尾报错。现以应用/启动器退出码0、原始AMReX finalized标记及完整窗口复核，已从既有日志恢复，无重跑。原始失败日志与postprocess_recovery.json保留。

复现绘图（仓库根目录执行）：

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/optimization_ladder/analyze.py /home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/optimization_ladder/round1/job626333 --output-dir /home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/data_post_processing/results/optimization_ladder_C128000
```
