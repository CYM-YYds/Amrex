# 二维圆柱 BTDF1 → OSI 续算实验

本目录是本次任务的复现入口，保留，不作为默认方腔程序的运行配置。
实验源码位于 `runs/cylinder_btdf1_restart_520000/build/src`；正式 `src/` 和默认 inputs 不变。

- `prepare.py [新实验目录] [--source-only]`：复制 BOX3D_OSI 源码，移植原算例的压力型 He–Luo、绕流边界、平滑三点 IBM 核与加密判据；原检查点仅复制，不原地修改。目录已存在则拒绝覆盖。
- `inputs`：本任务固定参数，Re 按圆柱直径计算，初始步 520000，目标步 584000。
- `submit.sh`：单 GPU、单 MPI rank。默认执行短测；显式 `CYLINDER_STAGE=production` 才执行长续算，并要求同一冻结程序的验证记录。
- `verify.py <实验目录>`：检查接受轮的 A–B/OSI 全 valid DDF 与初始速度导入；检查 520032→520064 原生重启的全 valid DDF。
- `verify.py <实验目录> production`：生成 `production/timing_windows.csv`、`summary.json`、`summary.md`；长任务退出时自动执行，也可运行中手动读取进度。

在仓库根生成一个全新的副本，然后进入生成目录编译：

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/cylinder_btdf1_restart/prepare.py /完整路径/新实验目录
cd /完整路径/新实验目录/build
GEN_CCDB=0 AMREX_BUILD_DIM=2 AMREX_BUILD_PRECISION=DOUBLE /home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/scripts/compile.sh
cp main2d.gnu.TPROF.MPI.CUDA.ex ../app.ex
```

提交须在 BOX3D_OSI 算例目录执行，`CYLINDER_TASK_DIR` 默认为本次实验目录：

```bash
dsub -x 'all;CYLINDER_STEPS=64;CYLINDER_ROUND=accepted;' -s scripts/temporary/cylinder_btdf1_restart/submit.sh
dsub -x 'all;CYLINDER_STAGE=restart;CYLINDER_ROUND=accepted;' -s scripts/temporary/cylinder_btdf1_restart/submit.sh
# 返回仓库根执行验证；通过后方可提交正式续算。
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/cylinder_btdf1_restart/verify.py projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/cylinder_btdf1_restart_520000
# 回到 BOX3D_OSI 算例目录：
dsub -x 'all;CYLINDER_STAGE=production;' -s scripts/temporary/cylinder_btdf1_restart/submit.sh
```

新实验须向每条 dsub 显式传入 `CYLINDER_TASK_DIR=/完整路径/新实验目录`，不要复用旧目录。
所有实际命令、输入、程序及 SHA256 都保存在各运行子目录。

## 耗时口径与限制

`perf(s)` 是报告窗口累计秒数；除以 `steps` 才是每个粗层步的均值。
`particle_total` 包含 `particle_macro/force/sum/ghost`，不可重复相加。
`particle_reduce` 含受力归约及 CdCl 写文件；`particle_redistribute` 为重网格后的颗粒迁移。
三者相加可统计整个颗粒路径，但迁移属于 regrid，不应全部除以 solv。
细分计时等待 GPU，会有同步开销；本实验为保留原边界时间语义而分配边界暂存缓冲，其开销包含于 total/solv。
`total` 不含流场与 checkpoint I/O；程序 `Total Time` 与 `resource_usage.txt` 提供更外层的耗时。
MLUPS 沿用 BOX3D_OSI 的层级加权 uncovered 网格更新计数，不是 GPU 实际发射 cell 数。

原程序与迁移程序保留相同物理和 IBM 公式，但 AMR 调度、掩码、插值/平均仍使用 BOX3D_OSI 框架。
早期未完整初始化宏观场的版本存在重启小差异；最终接受版的 520032→520064 对照差值为 0。正式任务直接导入原始 520000 步检查点，短程结果不外推至其他重启条件。
64 步短测不能代替长期稳定性、收敛性或网格无关性验证。失败轮保留以供排错，不混入接受结果。
