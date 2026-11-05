# 碰撞批量提交：额外实验

原始材料：/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/optimization_ladder/round2_collision/job626391。节点：whshare-agent-14。程序、源码差异、输入、实际命令、原始日志和退出状态均保留。

从主图job626333冻结源码扩展，正式源码不改。仅比较已启用插值批量时逐Fab碰撞与跨Fab批量碰撞；均为256线程、无坐标复用、边界优化关闭、通信重叠关闭，其余参数与主图一致。

同一程序、同一节点、同一GPU，先对照→实验，再实验→对照。每次独立从624717/C step128000恢复并推进5000粗步，共两对。平均MLUPS按每组20个500步窗口累计有效更新量 / 累计perf.total秒数计算，含重网格；分子由日志MLUPS_total×total恢复，受打印精度影响。

| 方案 | 平均MLUPS_total | 相对本作业对照 | 累计total/s |
|---|---:|---:|---:|
| 仅插值批量（对照） | 1793.10 | +0.00% | 294.629 |
| 插值＋碰撞批量（实验） | 1606.64 | -10.40% | 328.821 |

逐对结果：

| 对照顺序 | 对照MLUPS | 实验MLUPS | MLUPS变化 | total变化 |
|---|---:|---:|---:|---:|
| 对照→实验 | 1793.39 | 1606.73 | -10.41% | +11.62% |
| 实验→对照 | 1792.80 | 1606.55 | -10.39% | +11.59% |

两组每窗口/逐层碰撞calls、launch_cells及有效更新量一致。64步真实checkpoint动态AMR回归：本作业对照与主图冻结对照checkpoint一致，批量实验与本作业对照checkpoint一致，见validation.json。

碰撞任务只存当前布局派生的区域/Fab几何/索引，沿现有布局缓存的重建和clear失效，不保存状态数组地址或相位。执行时获取当前状态/掩码/phase；数值函数、掩码与ghost范围相同，阶段末同步保留。批量任务建表成本计入重网格计时。

只有两对本配置测量，不外推其他网格、精度、线程数、多GPU或长期物理精度。作为额外实验单列，不混入已采用优化主图；不将不同节点作业的绝对MLUPS直接作差。

复现绘图（仓库根执行）：

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/optimization_ladder/analyze.py /home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/optimization_ladder/round2_collision/job626391 --collision-extra --output-dir /home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/data_post_processing/results/optimization_ladder_C128000/collision_batch_extra
```
