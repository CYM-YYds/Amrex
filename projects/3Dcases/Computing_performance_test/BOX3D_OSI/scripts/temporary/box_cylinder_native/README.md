# 原生 BOX 二维圆柱：参数化边界与宏观场初始化

本任务使用正式 `src/` 构建的程序，不生成替代求解器源码。
复用 Qian/Guo、原四点 IBM、OSI、AMR、边界面任务缓存及批量发射；新增边界类型与参考速度/长度配置。
旧算例仅提供几何、Re、网格、速度、加密阈值数值及520000步宏观状态。

- `inputs`：800²/L3，圆心(0.5,0.5)，半径0.0125，速度0.03，直径参考Re40。
- `convert_macro.py source native_template output`：读取旧压力矩，得到 rho_BOX=p_old/p0_old（BOX参考密度1）和 u=sum(f_old e_old)/p0_old；重建 BOX Qian 平衡分布。不保留旧非平衡分量。颗粒来自 BOX 原生模板，不复用旧502点颗粒字段；本次为原生503点、默认IBM权重。
- `submit.sh`：复用现有 hostfile/命令记录工具，用同一冻结的正式BOX程序运行各阶段。模块、MPI/GPU信息、输入、命令和程序哈希均保留。
- `verify.py`：复用现有 FAB 读取器，检查对应范围、初始化矩与程序/输入绑定。额外未通过的64步布局对照与非输出时刻冷重启单独记录，不作长期等价认证。

正式计算保存在 `runs/box_cylinder_native_520000/production/`；短测在 `runs/temporary/box_cylinder_native/`。
最终源代码快照是正式目录的 `src_snapshot_final/`；`native_manifest.json` 包含逐文件哈希和复用检查。
早期错误迁移副本保留在另一个目录，不混入本次结果。

## 复现

在算例目录编译正式代码：

```bash
GEN_CCDB=0 DIM=2 PRECISION=DOUBLE ./scripts/compile.sh
```

在全新的任务目录冻结该程序为 `app.ex`、本目录的配置为 `inputs`；原生模板阶段：

```bash
dsub -x 'all;NATIVE_STAGE=seed;NATIVE_TASK_DIR=/完整路径/新任务目录;' -s scripts/temporary/box_cylinder_native/submit.sh
```

转换命令从仓库根运行（目标不能已存在）：

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/box_cylinder_native/convert_macro.py projects/Cylindertest/Cylinder2D_BTDF1/chk00520000 /完整路径/新任务目录/seed/chk00000000 /完整路径/新正式目录/converted/chk00520000
```

后续短测使用 `NATIVE_STAGE=matrix/full/aligned`；`final` 将重放最终程序所需的对应范围测试，目录已存在时拒绝覆盖。
每条实际运行命令以运行子目录的 `command.txt` 为准。`production` 先检查已冻结的 `validation.json` 与程序/输入哈希，随后计算520000→584000；长算退出后复用统计读取器生成CSV及汇总。生产入口默认正式任务目录，仅复现时须按脚本中已声明的任务路径准备独立材料。

## 验证边界

最终程序32步完整网格 A–B/OSI 对照误差约5.6e-16；输出对齐的32→64重启对照为0。
附加的64步独立A–B/OSI试验出现一块网格差异（共同Fab的DDF最大差约1.87e-7），尚未归因；不声称长期数值等价。
非输出时刻16步检查点的冷重启也存在网格差异，不作为已通过结果。正式任务每4000步输出、每8000步保存检查点，按已测试的输出对齐顺序保存。
本次长算是模型切换初始化试算；完成步数、有限值检查及耗时图均不代表收敛或网格无关性通过。
