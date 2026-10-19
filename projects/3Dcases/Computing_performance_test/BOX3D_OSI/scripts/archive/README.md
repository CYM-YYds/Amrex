# 历史专项实验入口

这里保留退出日常入口的 12 个实验驱动。归档不等于实验已失效、已被其他验证等价替代，
也不代表全部历史依赖仍齐全。日常构建和生产提交使用[算例 README](../../README.md)中的入口。

这些脚本仍以 BOX3D_OSI 为工作根目录，公共 MPI 机制仍引用 `scripts/osi_experiment_common.sh`。
运行时显式加载各自原有模块，保留原资源、账户、作业名、日志名、覆盖参数与输出位置。
如需重新使用，从算例目录执行 `dsub -s scripts/archive/<脚本名>`；本次维护没有提交这些作业。

| 脚本 | 保留的实验职责 | 依赖与输出/证据 |
|---|---|---|
| [submit_ab_four_level_smoke.sh](submit_ab_four_level_smoke.sh) | 四层 A-B 推进、输出与 canonical checkpoint 检查 | `AB_*` 参数；输出 `tmp/ab_four_level_smoke_<job>/` |
| [submit_interp_ab.sh](submit_interp_ab.sh) | 三种插值从 step9000 运行至10000 | 依赖算例根 `chk00009000`，当前缺失；不能直接重现 |
| [submit_interp_scale_perf.sh](submit_interp_scale_perf.sh) | 固定1000步插值性能实验 | 旧账户、CUDA12.1、要求分配表第三列非空；不能当作当前环境默认配置 |
| [submit_osi_full_q_matrix.sh](submit_osi_full_q_matrix.sh) | A-B、双/单rank OSI、周期非均匀初值四组 | 输出 `runs/full_q_matrix_<job>/` 并保存 app/inputs/命令；历史 job606276 |
| [submit_osi_mpi_ab_perf.sh](submit_osi_mpi_ab_perf.sh) | 8 Fab、1000步同层 A-B/OSI MPI 性能 | HMPI；可用附加覆盖参数增加实验组；不能代替多层性能验证 |
| [submit_osi_multilevel_ab.sh](submit_osi_multilevel_ab.sh) | 共同step96状态分支后的四层逐值比较 | 输出 `tmp/multilevel_ab_<job>/`；保留其独立 oracle 和原日志判据 |
| [submit_osi_multilevel_mpi_perf.sh](submit_osi_multilevel_mpi_perf.sh) | 三层共同种子后的双rank固定布局性能 | 输出 `tmp/multilevel_mpi_perf_<job>/`；可用 `PERF_SOURCE_RUN_DIR` 复用种子，关联历史 job602049 等 |
| [submit_osi_parallelcopy_multilevel_check.sh](submit_osi_parallelcopy_multilevel_check.sh) | 共同step32状态到step40的平均/传递对照 | 输出 `tmp/osi_parallelcopy_check_<job>/`；host-staging默认，device-direct未验收；历史 job602116/602119 |
| [submit_re1000_single_gpu_128k_ab_osi.sh](submit_re1000_single_gpu_128k_ab_osi.sh) | OSI生成step3200种子后，同GPU顺序执行长程A-B/OSI | 输出 `tmp/re1000_single_gpu_128k_ab_osi_<job>/`；保留 job596770/596771 关联目录 |
| [submit_single_gpu_multilevel_1000_compare.sh](submit_single_gpu_multilevel_1000_compare.sh) | 单GPU共同step96状态后的1000步比较 | 输出 `tmp/single_gpu_multilevel_1000_<job>/`；保留 job602325/602327 关联目录 |
| [submit_tiny_profile.sh](submit_tiny_profile.sh) | 同步 TinyProfiler 的1000步专项统计 | 旧账户、CUDA12.1/GCC10.3.1；原静态契约测试已更新路径，不代表当前运行环境验收 |
| [submit_osi_mpi_direct_multilevel_smoke.sh](submit_osi_mpi_direct_multilevel_smoke.sh) | 两GPU、动态AMR、64步锁步检查 | 历史 job606170；保留 valid 对照与 physical-ghost 差异的证据边界 |

迁移前源码可定位到 `b20e767`，本次只修正目录层级和公共依赖位置，没有改写这些实验的数值判据。
重现实验仍需当时的源码、可执行文件、完整输入和覆盖参数；脚本读取当前 `config/inputs`
不意味着当前输入等同于历史输入。历史状态与验证范围见[交接记录](../../docs/current_status.md)。
本次未删除或迁移 `tmp/`、`runs/`、原始数据、日志或检查点。
