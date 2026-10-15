# 整合 BOX3D_OSI 实验脚本与收敛后处理

目的：减少现役实验入口中重复的 hostfile/MPI 启动、失效 direct 参数和两份收敛日志解析，同时保留每个入口的资源与实验策略。

范围：仅本算例 `scripts/`、`data_post_processing/`、脚本模拟检查及直接相关说明。`osi_experiment_common.sh` 提供 hostfile、MPI 启动和命令记录辅助逻辑；各入口仍指定环境、可执行文件、rank/GPU 数与覆盖参数。64/128/256 步锁步入口共用参数函数，保留各自 DSUB 头、步数、失败策略和检查间隔。现役脚本移除已失效的 direct 开关；程序继续拒绝旧 `=0` 请求。缺失 `config/inputs_osi` 的历史入口与旧 batch 解析器保持原样。两张收敛图共用严格的日志解析、采样字段和按绝对 step 去重，支持重复 `--log` 及 `--output`，第二张图另支持 `--csv-output`；原默认实验和图表布局保留。

验证：所有算例脚本通过 `bash -n`，模拟启动器检查 hostfile、HMPI/UCX 参数、rank/GPU 映射、锁步参数和失败码传播。仓库根 `.venv/bin/python` 的语法检查、历史日志解析和两张图重画通过：586660 系列得到趋势/正式记录 160/50 条，596154 系列得到 288/90 条；重复 step 以后传入记录为准，缺采样字段会失败。图与 CSV 输出保存在忽略的 `runs/architecture_dedup_stage3_validation_20261010/`，没有覆盖历史默认图或日志。文档相对链接及 CRLF-aware diff 检查通过。

限制：GPU-aware MPI 脚本仅验证启动参数，没有可用环境的运行证据。历史用途入口未作为当前验收入口。
