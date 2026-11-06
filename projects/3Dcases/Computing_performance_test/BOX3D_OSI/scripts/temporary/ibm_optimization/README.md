# IBM 核权重复用试验

临时验证任务，保留输入、程序、基线头文件、差异、日志、检查点与分析入口。
数据位于 `runs/temporary/ibm_optimization/`。

本轮只优化二维/三维四点核权重复用；没有改变核支撑、力公式、原子累加顺序，
也没有优化整层 SumBoundary。每次 kernel 调用内部持有一维权重数组，无跨步缓存。
已有 GPU kernel 扩展即可完成，不增加生产 helper 或额外配置分支。

验证命令（仓库根）：

```bash
ASAN_OPTIONS=detect_leaks=0 TEST_SOURCE=ibm_weights_test.cpp \
  bash projects/3Dcases/Computing_performance_test/BOX3D_OSI/tests/run_dimension_kernels_test.sh
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/ibm_optimization/analyze.py \
  projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/ibm_optimization
```

GPU 配对任务 626770，同一 GPU 上三组交错顺序，从生产 chk00584000 推进到 584032；
为固定工作量关闭重构，不输出流场，末步保存检查点。submit.sh 拒绝覆盖已有目录。
首次提交 626769 因脚本缺少执行权限未启动程序，日志保留。
输入、程序哈希见数据目录 manifest.json；原正式程序源码与当前源码只有 Kernels.H 不同。

逐值检查只比较未被细层覆盖的有效单元。被覆盖粗层数据中有非有限存储值，
不把它们当作实际参与推进的有效状态；报告保留对应计数。
短测试不能证明长期等价、三维 GPU 运行正确性或多 GPU 性能；SumBoundary 耗时波动不归因于此优化。

## 局部力汇总主优化

入口已扩展为参数化复用，不复制按作业号/路径变化的脚本。
材料：`runs/temporary/ibm_local_sum/`，生产路径统一使用局部汇总，已删除 `ibm.local_force_sum` 选择开关；整层汇总仅用于验证参考。
`ibm.verify_local_sum=true` 每次与整层参考比较全部力场（含 ghost）；该开关会增加重复工作，性能测试必须关闭。

验证及性能提交时，先冻结 TASK 下的 inputs、baseline.ex、optimized.ex、app2d.ex、app3d.ex。
从算例目录、加载调度环境后提交：

```bash
dsub -x 'all;IBM_TASK_DIR=/绝对路径/任务目录;' -s scripts/temporary/ibm_optimization/submit.sh
dsub -R 'cpu=16;mem=65536;gpu=2' \
  -x 'all;IBM_TASK_DIR=/绝对路径/任务目录;IBM_NPROCS=2;' \
  -s scripts/temporary/ibm_optimization/submit.sh
```

性能各组合使用不同任务目录以免覆盖。逐力场验证使用同一入口，额外传入
`IBM_STAGE=validation;IBM_VALIDATION_TAG=_v3;`，按 NPROCS 分开保存。物体保持在求解器支持的域内范围。

从仓库根重新分析已保留材料：

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/ibm_optimization/analyze.py \
  projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/ibm_local_sum \
  --local-sum --validation-tag=_v3
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/ibm_optimization/analyze.py \
  projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/ibm_local_sum/two_gpu --local-sum
```

多 rank 日志每个 rank 都打印一行；分析对各计时器取 rank 最大值，不将计时相加。


最终统一生产路径的验收材料位于 `runs/temporary/ibm_local_sum/closeout/`。
删除旧开关后重新构建并通过单/双GPU的10项逐力场检查，重新汇总：

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/ibm_optimization/analyze.py \
  projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/ibm_local_sum/closeout \
  --local-sum --validation-only
```

该模式只生成 validation.json/md，不要求新旧生产路径并存，也不产生性能结论。
