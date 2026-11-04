# C 阈值 A-B 本地输出延长验证

目的：job625616在首次共享盘流场写入返回EIO后，验证本地输出和校验回传路线。

范围与改动：扩展现有paired_paths.py环境参数，选择单独local_original、终点32000步、100ms采样及实验根目录；默认三路径6400步诊断不变。计算或回传失败向调度器返回非零。沿用冻结求解器，不修改数值路径。提交job625774，记录见runs/temporary/C_AB_local_output_validation。

验证：Python语法解析、提交Shell bash -n、git diff --check通过。调度器已接受提交；运行、完整场输出及checkpoint、逐文件回传结果待作业结束验证。不将提交成功当作I/O问题解决。
