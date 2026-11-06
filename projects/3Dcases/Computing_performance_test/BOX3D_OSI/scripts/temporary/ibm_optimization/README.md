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
