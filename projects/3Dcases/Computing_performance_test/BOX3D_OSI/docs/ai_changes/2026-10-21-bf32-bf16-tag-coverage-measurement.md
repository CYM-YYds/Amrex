# BF32/BF16 原始打标与实际覆盖的逐层对照

- 目的：按用户要求重新运行 BF32、BF16，测量所有可细化层级的原始判据打标数与最终细网格覆盖数，避免用另一步数的打标比例代替同次实测。
- 作业：BF32 为 [616234](../../runs/20261021_120737_job616234/)，BF16 为 [616235](../../runs/20261021_120737_job616235/)。两次均从 job615364 的 chk00256000（step=256000）重启，目标 step=259200，共 3200 coarse steps；相同 app、Re=3200、域长 1、基础网格 128³、最高 level 2、当前 state_error_6、grid_eff=0.7、n_error_buf=1、max_grid_size=128、细化比 2。仅三个方向 blocking_factor 的命令行覆盖不同（各层 32 或 16）；工作区默认 BF16 和用户已有空白修改不动。
- 资源：两作业各单节点 4 GPU / 4 MPI ranks，CPU=128、内存=196608 MiB；BF32 在 whshare-agent-174，BF16 在 whshare-agent-14，独立节点运行。用现有采样包装器和退出监督。此任务测量覆盖，不以两节点耗时作性能结论。
- 诊断：复用默认关闭的 verification.refinement_diagnostics，本次显式开启；记录每次 ErrorEst 的 valid 涡量（含 covered、排除 ghost）和原始打标，在同次 regrid 后记录实际网格和粗细覆盖 mask。level 0→1、level 1→2 各测 100 次；最高 level 2 没有向更细层打标，记录其网格即可。关闭 PlotFile 和 checkpoint 写出，避免此前 VisMF 写失败；收敛判断保留开启，只有末次到正式采样点。
- 定义：对每个父层分别取 T=缓冲/粗化/投影前原始打标数、C=同次 regrid 后细单元数/8。用户指标 E=C/T-1，以原始打标为分母；另列 F=1-T/C，以实际覆盖为分母。统计按层分开，不把不同分辨率单元数直接相加。这是计数比例，不是原始标记与覆盖区域空间交集或数值精度损失的测量。
- 工具：复用前次运行中保留的 analyze_refinement_diagnostics.py 汇总日志，临时 compare_tag_coverage.py 连接同一时间、同一层级的原始与最终数据，交叉验证 C 与 CF_MASK_DIAG 的 covered、covered_expected 一致；输出每次 regrid 的 CSV、最终 259200 步 JSON 和 Markdown 表格。复现工具副本及结果位于 [对照目录](../../runs/20261021_bf_tag_coverage_comparison/)。
- 构建：GEN_CCDB=0 MAKE_J=8 ./scripts/compile.sh --no-submit 成功、目标最新，日志 logs/compile/compile-20261021T120715.log。app SHA256 为 c5cb56aa5df684e37699638e48580ef35593ec79f7f5d326fc4ad7150fc77f67。两运行均保留独立 app、源码归档、工作区差异、输入快照/覆盖、git_head.txt 和显存采样。
- 终点结果（step=259200）：level 0→1，BF32 原始打标 1563546（74.5557% 全域），BF16 为 1564758（74.6135%），实际覆盖均为 2097152 父单元、生成 8 个细块，E 分别为 34.1279%/34.0240%，F 为 25.4443%/25.3865%。此层几乎没有覆盖改善。
- 终点结果（level 1→2）：BF32 原始打标 7693248（45.8553% 全域），实际覆盖 16777216 父单元，E=118.0772%、F=54.1447%；BF16 原始打标 7654327（45.6233%），实际覆盖 11518464 父单元，E=50.4830%、F=33.5473%。实际细单元从 134217728 降至 92147712（减少 31.3446%），细块从 64 增至 275。
- 完整结果：[Markdown 表格](../../runs/20261021_bf_tag_coverage_comparison/comparison.md)、[JSON](../../runs/20261021_bf_tag_coverage_comparison/comparison.json)、[逐次 CSV](../../runs/20261021_bf_tag_coverage_comparison/tag_coverage_timeseries.csv)。CSV 含 400 行测量，每个方案两个父层各 100 次，覆盖 step=256032 至 259200 的全部 regrid。另存 raw_valid 和 domain 两种分母，避免把第一次尚未铺满全域的 level 1 valid 打标比例误当全域比例。
- 验证：两作业 exit_status.json 均为 0、completion_seen=true，8 个 rank 均返回 0，两个监督器均为 SUCCEEDED。400 组原始打标数与阈值超限数一致，非有限和负涡量计数均为 0；400 组 fine_cells/8 与同次 covered/covered_expected 全部相等。终点与采样数量严格校验通过。app 和 inputs 内容相同、覆盖参数仅 BF 三项不同的检查通过；Python AST、记录链接与 CRLF 兼容 diff 检查通过。
- 边界与收尾：这些是打标/覆盖计数证据，不是涡量计算准确性、稳态收敛或性能收益的证明。作业均已退出，测量工具的独立复现副本保存在对照目录；删除本次 scripts/temporary 下无后续依赖的汇总入口，不清理其他任务工具及原始结果。用户已有 inputs 空白修改保持未提交。
