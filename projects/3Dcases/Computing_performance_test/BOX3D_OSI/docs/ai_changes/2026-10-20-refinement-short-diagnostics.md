# 涡量加密判据的 512 步短诊断

- 目的：区分涡量阈值导致的大范围打标与 AMReX 网格生成对覆盖范围的扩大，不改变判据或网格参数。
- 实现：新增默认关闭的 `verification.refinement_diagnostics` 开关。ErrorEst 末尾读取同一次实际使用的 valid 涡量（含 covered 粗单元、排除 ghost），输出范围、均值、非有限/负值计数、实际打标数和固定阈值超限数；RefineMesh 重建缓存后复用现有 PrintRegridDiagnostics 输出新网格及覆盖 mask。诊断只读，不建立新的数值路径、派生状态或缓存，不启用 A-B 参考数组；包含额外归约/同步，不能用此运行衡量性能。
- 作业：[615978](../../runs/20261020_203826_job615978/)，4 GPU / 4 MPI ranks，从 job615364 的 chk00256000 续跑至 step=256512（512 coarse steps、16 次 regrid），正常退出。保持 Re=3200、域长 1、基础网格 128³、最高 level 2、state_error_6、n_start=-2、n_inc=1、编译期 max_ref_level=3、grid_eff=0.7、blocking_factor=32、n_error_buf=1。关闭 PlotFile/checkpoint 写出以只诊断网格生成；收敛开关保留开启但 512 步未到采样间隔，不作收敛判断。
- 溯源：源码基线 fd9ede0（本任务前用户已合并二维兼容实现），诊断工作区差异、源码归档和独立 app 已保存。app SHA256 为 c5cb56aa5df684e37699638e48580ef35593ec79f7f5d326fc4ad7150fc77f67；构建日志 logs/compile/compile-20261020T203628.log。
- 实测：末次 regrid 前，level 0 有 1556703/2097152=74.2294% 单元打标，生成 level 1 全覆盖；level 1 有 7611082/16777216=45.3656% 单元打标，生成 level 2 为 134217728 单元、64 boxes，即全覆盖。level 2 覆盖率从第一次 26.9043%、第二次 78.8574%，在多数中间步骤约 80%，到最后一次变为 100%。
- 涡量检查：16 次 regrid、两个打标层的 32 组统计中，实际 tag 数均等于当前公式对应阈值的超限数，非有限值和负值均为 0。末次 level 0 涡量均值 0.1372002、最大值 4.0838450；level 1 均值 0.1371888、最大值 8.2736736。以上只验证当前场的统计与打标自洽，不证明涡量离散或流场数值正确。
- 阈值反事实：同一末次流场上，若把映射最高层号从编译期 3 统一到实际 2，level 0/1 阈值将从 0.03125/0.0625 翻倍为 0.0625/0.125；相应超限比例为 45.2971%/23.8165%。这里只在冻结场上统计，未改变运行判据，不能保证新运行的覆盖比例或演化结果。
- 网格机制：AMReX_AmrMesh.cpp 在 ErrorEst 后依次缓冲 tags、按 blocking_factor/ref_ratio（本例 16）粗化，再执行嵌套约束/投影、聚类；ClusterList::chop/new_chop 只在 cluster 效率低于 grid_eff 时继续切分。因此原始单元打标比例与最终覆盖率不相等，也不能直接把原始比例与 0.7 比较来定量归因；本次未分离各个网格参数的独立影响。
- 结论：全覆盖由大范围打标与后续网格生成共同造成，未发现当前比较公式额外误标的证据。论文思路并未被这次实验否定，但当前最高层号映射、单元打标及块划分与论文不完全对应；下一步应先统一层号含义，再以相同 checkpoint 做短对照，必要时单独检查网格划分参数。此次不自动调阈值、不新增长作业。
- 复现：[原始日志](../../runs/20261020_203826_job615978/run.log)、[结构化汇总](../../runs/20261020_203826_job615978/refinement_summary.json)、[CSV](../../runs/20261020_203826_job615978/refinement_stats.csv) 已保存。使用仓库 `.venv/bin/python` 执行运行目录内 `analyze_refinement_diagnostics.py <运行目录>` 可重算统计；原始输入和 overrides.txt 定义运行参数。
- 验证：目标三维 CUDA/MPI 编译通过，512 步完成，4 个 rank 退出码均为 0，exit_status.json 为 0、调度监督器为 SUCCEEDED；32 组阈值/打标一致性检查通过。Python AST、README/记录链接与 CRLF 兼容 diff 检查通过。独立临时汇总脚本已在运行目录保留复现副本后从 scripts/temporary 删除，其他任务工具和历史结果不清理。
