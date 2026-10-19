# 合并锁步与 Re=1000 提交预设

- 目的：按用户确认的第一批维护方案，将五个仅预设不同的提交脚本收敛为两个入口。
- 影响范围：仅 BOX3D_OSI 的五个旧提交入口、两个替代入口、既有脚本回归检查和直接相关文档；脚本净减少三个。求解器、构建配置、生产 inputs、后处理、历史 runs/日志/检查点和结果数据未改动。
- 主要改动：
  - 锁步入口继续复用 `osi_lockstep_args.sh`；三个预设保持 `(64,true,32)`、`(128,false,32)`、`(256,true,1)` 的步数、遇差异继续策略和检查间隔。
  - Re=1000 入口通过 `ab/osi` 选择 `lbm.stream_mode=0/1`；其余参数和用户附加参数在前、实验固定参数在后的覆盖顺序保持原行为。锁步仍整体设置覆盖参数。
  - 从提交节点直接执行新入口时调用 `dsub`，传入原作业名和标准输出/错误日志名，以 `-x all;变量=预设;` 显式传递选择；通过调度器提供的 `CCS_JOB_ID` 区分作业内执行，避免重复提交。单节点、1 GPU、32 CPU、49152 MiB 和其余调度声明保持一致，未新增时限。
  - 不带参数时选择 `64` 和 `osi`；位置参数优先于 `BOX3D_LOCKSTEP_STEPS` / `BOX3D_RE1000_MODE`，非法预设和多余参数返回 2。`--help` 不提交作业。
  - 删除下表中已承接的旧入口，同步 README 和 current_status 的替换说明。没有增加兼容 wrapper 或扫描脚本。

## 旧入口与新调用

以下路径均相对算例根目录；新命令在提交节点执行。

| 删除的旧入口 | 替代调用 |
|---|---|
| `scripts/submit_osi_lockstep_alllevels_64.sh` | `./scripts/submit_osi_lockstep.sh 64` |
| `scripts/submit_osi_lockstep_alllevels_128.sh` | `./scripts/submit_osi_lockstep.sh 128` |
| `scripts/submit_osi_lockstep_alllevels_256.sh` | `./scripts/submit_osi_lockstep.sh 256` |
| `scripts/submit_re1000_128k_ab.sh` | `./scripts/submit_re1000_128k.sh ab` |
| `scripts/submit_re1000_128k_osi.sh` | `./scripts/submit_re1000_128k.sh osi` |

旧脚本基线为维护前提交 `6159defe06d50807a8b31632b09bb86c6e7e5719`。
历史复现还需相应实验的可执行文件/源码版本、完整输入快照和覆盖参数；
从 Git 取回入口或使用新入口，均不能替代这些材料。主提交脚本继续保存
`runs/<timestamp>_job<job-id>/inputs`、`overrides.txt`、`command.txt`、`manifest.txt`
和 `run.log`，本次不回写历史目录。

## 验证

- 修改前在隔离临时目录保存五个原脚本、SHA256、调度器头及模拟执行的完整覆盖参数。
  使用根 `.venv/bin/python` 对照新入口：五组全部参数及其顺序、作业名、两类日志名、
  资源和其余调度声明均一致；附加 `sentinel=1 max_step=7` 验证覆盖顺序。
- 扩展既有 `tests/check_osi_script_helpers.sh`，模拟提交节点到作业节点的再次进入，
  覆盖全部五组、工作目录、参数覆盖、非法预设、帮助不提交、作业内失败码 37、
  提交失败码 19；测试通过。原 hostfile、HMPI/UCX 启动、锁步参数检查继续通过。
- 另检查默认选择、环境变量选择、位置参数优先级、作业内直接执行和多余参数拒绝，全部通过。
- 所有算例 `scripts/*.sh` 与修改后的测试通过 `bash -n`；文档本地链接和新入口路径有效，
  `git diff --check` 通过。旧入口引用只保留在明确的历史/替换说明中，无现役脚本调用。
- 只读取本机 `dsub -h` 确认命名、日志与环境选项，未提交真实作业、调用 MPI 或启动 CFD。
  上述验证证明预设与提交参数保持一致，不构成真实调度器集成或求解器数值回归验证。

其余后处理合并候选、缺输入的旧入口及证据不足项按审查结论暂留，本次未处理。
