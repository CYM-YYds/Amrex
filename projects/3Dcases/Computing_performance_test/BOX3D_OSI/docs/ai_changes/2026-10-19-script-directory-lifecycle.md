# BOX3D_OSI 脚本目录完整生命周期整理

## 目的与范围

按用户确认的完整 30 项方案，明确日常入口、内部依赖、历史实验和未验收环境，
消除固定网格重复入口和没有算例逻辑的清理转发；不把归档移动计作消重。
本次只改算例辅助脚本、直接相关测试和说明，求解器、config、可执行文件、
原始数据、历史图表、tmp/runs/logs 和检查点均不变。
维护前算例文件基线为 `b20e767`，原始文件和 SHA256 另存于本次隔离验证目录。

## 逐文件处置

原路径均为 `scripts/<文件名>`。分类为 KEEP 10、MERGE 2、PROMOTE 1、ARCHIVE 13、DELETE 1、REVIEW 3。

| 文件 | 分类及证据 | 最终位置/承接方式 |
|---|---|---|
| compile.sh | KEEP，现役 README 构建入口与算例日志约定 | 原位 |
| submit_guarded.sh | KEEP，现役推荐生产提交及外部监督 | 原位 |
| submit.sh | KEEP，上述入口和两组实验实际调用的快照/运行实现 | 原位 |
| submit_osi_lockstep.sh | KEEP，已确认合并的现役锁步入口 | 原位 |
| submit_re1000_128k.sh | KEEP，已确认合并的 Re=1000 预设入口 | 原位 |
| guard_run.py | KEEP，submit.sh 与生命周期测试直接调用 | 原位，内部依赖 |
| watch_job.py | KEEP，submit_guarded.sh 与生命周期测试直接调用 | 原位，内部依赖 |
| run_rank.sh | KEEP，submit.sh 指定的应用退出凭证机制 | 原位，内部依赖 |
| osi_experiment_common.sh | KEEP，多份驱动及初始化验证直接依赖 | 原位，内部依赖 |
| osi_lockstep_args.sh | KEEP，统一锁步入口与脚本回归直接依赖 | 原位，内部依赖 |
| clean.sh | DELETE，只有根清理脚本转发，无算例行为，未发现调用者 | 删除该别名；没有执行根清理脚本 |
| submit_osi_stage3_mpi.sh | MERGE，周期固定网格64步seeded锁步检查 | tests/submit_osi_fixed_grid_check.sh periodic |
| submit_osi_amrex_plan_nonperiodic.sh | MERGE，同资源/推进的非周期预设 | tests/submit_osi_fixed_grid_check.sh nonperiodic |
| plot_run_performance.py | PROMOTE，现有日志接口与通用性能图；原1000步标注须明确 | data_post_processing/plot_run_performance.py |
| plot_transfer_cost_pies.py | ARCHIVE，内嵌旧计时常量、原日志和步数未定位 | data_post_processing/archive/plot_transfer_cost_pies.py |
| submit_ab_four_level_smoke.sh | ARCHIVE，保留四层A-B及输出/checkpoint独立检查 | scripts/archive/，同名 |
| submit_interp_ab.sh | ARCHIVE，固定step9000重启实验，所需检查点当前缺失 | scripts/archive/，同名并标明复现缺口 |
| submit_interp_scale_perf.sh | ARCHIVE，旧账户/CUDA12.1和特定分配表条件的固定实验 | scripts/archive/，同名 |
| submit_osi_full_q_matrix.sh | ARCHIVE，历史job606276四组独立对照与app快照 | scripts/archive/，同名 |
| submit_osi_mpi_ab_perf.sh | ARCHIVE，固定8 Fab/1000步同层MPI性能流程 | scripts/archive/，同名 |
| submit_osi_multilevel_ab.sh | ARCHIVE，共同step96状态后的四层逐值oracle | scripts/archive/，同名 |
| submit_osi_multilevel_mpi_perf.sh | ARCHIVE，关联tmp/multilevel_mpi_perf_*历史目录 | scripts/archive/，同名 |
| submit_osi_parallelcopy_multilevel_check.sh | ARCHIVE，job602116的step32→40比较与未验收device分支 | scripts/archive/，同名 |
| submit_re1000_single_gpu_128k_ab_osi.sh | ARCHIVE，关联job596770/596771，共同成熟种子后长程比较 | scripts/archive/，同名 |
| submit_single_gpu_multilevel_1000_compare.sh | ARCHIVE，关联job602325/602327，step96种子后1000步比较 | scripts/archive/，同名 |
| submit_tiny_profile.sh | ARCHIVE，保留旧统计与测试契约，模块含GCC10.3.1 | scripts/archive/，同名；更新静态测试引用 |
| submit_osi_mpi_direct_multilevel_smoke.sh | ARCHIVE，保留job606170两GPU/动态AMR的独立检查 | scripts/archive/，同名 |
| compile_cuda_aware.sh | REVIEW，独立OpenMPI构建，当前app来源及环境验收仍待核实 | scripts/experiments/cuda_aware/，同名 |
| submit_osi_mpi_device_correctness.sh | REVIEW，依赖独立app，生产host-staging证据不能外推 | scripts/experiments/cuda_aware/，同名 |
| submit_osi_mpi_device_perf.sh | REVIEW，同一未验收环境的性能实验 | scripts/experiments/cuda_aware/，同名 |

归档决定表示退出日常入口，不代表独立oracle可删或所有历史条件仍可复现。
归档索引保留职责、依赖、输出目录和已知缺口；未验收三件套说明其解除待决的条件。
现役 README 指向新的索引；旧变更记录与历史作业中的路径保留其历史含义。

## 行为与数量

- 15 个迁移 shell 脚本只改算例根目录层级和公共脚本引用，保持原资源、账户、模块、
  作业/日志名、命令参数、工作目录及结果输出位置。归档入口仍可按索引从算例目录显式提交，
  但本次没有提交；缺少历史输入或环境时不承诺可完整运行。
- 固定网格入口默认 periodic，支持 nonperiodic 与环境选择；保留原两种作业标识、几何参数
  分组、周期入口的兼容direct开关，以及追加 AMREX_RUN_ARGS 的顺序。继续复用已有 MPI helper，
  没有引入通用实验框架或替代求解器路径。
- 日志性能图解析和指标计算保持原样，新增 `--window-steps`（默认1000）用于正确标注时间窗，
  非正数、相邻记录窗口不匹配或缺窗时拒绝绘图；单条记录的窗口长度仍需外部配置确认。
  图内补来源、步数和归一化/字段参照，缺失旧细分字段绘为0的历史兼容逻辑及跨rank聚合缺口明确说明。
- 历史饼图的三组常量、计算和算例相对输出位置保持原样，仅补充出处/步数未知的标注。
- 顶层 scripts 文件数：30→10；其中用户入口（含旧专项入口）25→5、内部依赖5→5。
  原30个脚本跨目录整理后为28个，净减少2个；scripts递归仍有25个脚本，含12个归档、3个实验。
  全算例脚本数56→54。移动不算减少脚本，未新增兼容wrapper或配置副本；新增文件只有必要的
  合并检查入口、目录索引与本变更记录，回归检查复用既有测试文件。

## 验证及边界

- skill-creator 的 Skill 校验通过；该规则变更已单独记录并提交。
- 15 个迁移 shell 的调度器头逐字一致，除预期路径改动外源文本一致；在隔离夹具中
  对比全部15个首个 MPI/构建调用的选项、app argv、hostfile内容、工作目录和失败码113，均一致。
  固定网格两组的完整 MPI/app 参数、hostfile内容、工作目录和失败码也与旧入口一致。
  所有 module、MPI、app、make、SSH/Git相关执行均在夹具中替换或隔离，未启动真实构建/计算。
  首次失败测试不验证多阶段实验的后续数值输出，后续流程仅由路径改动之外的文本一致性支撑。
- 扩展既有脚本回归，检查固定网格两种预设、job再进入、旧作业/日志标识、工作目录、非法输入、
  帮助不提交和提交/运行失败码传播；原锁步/Re=1000与helper检查继续通过。
  TinyProfiler静态契约检查在新归档路径通过，不作为当前环境运行验收。
- 根 `.venv/bin/python` 对合成1000/3200步日志做迁移前后逐字段解析比较，全部一致；
  默认性能图全部曲线数值一致，3200步绘图与窗口不匹配/非正窗口拒绝通过。
  历史饼图常量和输出相对路径对照一致，在隔离目录重绘成功。
- 全部54个脚本完成shell语法/Python AST检查；README及新增说明的本地路径与链接、执行权限、
  移除入口的现役调用和Git空白检查通过。10个KEEP文件与迁移前SHA256一致。
- 对照和生成图均保存在 `/tmp/box3d-full-script-maintenance-_vt1y3q7/`，未覆盖用户结果。
  本次没有真实调度器提交、MPI运行、CFD数值回归或CUDA-aware环境验收，未将模拟检查称为数值等价。

后处理目录中原有的其他10个脚本、现有其他测试和科研结果不在本批整理范围内。
