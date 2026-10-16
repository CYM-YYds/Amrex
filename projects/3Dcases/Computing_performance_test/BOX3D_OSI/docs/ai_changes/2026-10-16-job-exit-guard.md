# 2026-10-16 作业退出监督与资源回收

## 问题与证据

613495 在 2026-10-15 18:26 已完成 256000 步、40 个速度场和 checkpoint，
AMReX finalized 已输出，但 2026-10-16 仍为 RUNNING、JOB_TIMEOUT=0。
原进程已被终止，计算节点拒绝 SSH，不能重建其当时的进程树。

复测 614521：移除 tee 后仍复现。rank_exit_0.json 的应用退出码为 0，
exit_status.json 的 launcher_returncode=0、elapsed_seconds=5.43，但调度状态仍 RUNNING。
因此不能把根因定为 LBM、MPI_Finalize 或 tee；故障边界至少延伸到应用和启动器
正常返回之后的调度收尾/残留任务管理。原作业精确根因仍待平台侧进程和 agent 日志确认。
614521 随即手动回收；613495 既有输出全部保留。

## 影响范围与改动

只改 BOX3D_OSI 提交入口，不改数值代码和仓库其他算例。

- scripts/submit.sh：调度硬时限 4 小时；直接记录 run.log；exec 进入监督器。
- scripts/guard_run.py：独立进程组、3 小时运行时限、finalized/全 rank 返回后
  60 秒退出宽限，异常清理和原子退出凭证；超时保存本作业进程组快照。
- scripts/run_rank.sh：记录真实应用返回码，区分应用未退出和启动器未退出。
- scripts/submit_guarded.sh、watch_job.py：提交节点独立 session 监督，完成凭证与
  RUNNING 冲突超过 60 秒时自动终止这个确定的 job；保留回收动作与终态。
- README：更新推荐入口、日志读取及时间限制配置。旧 runs 脚本不回写。

## 验证

- 三个 shell 脚本语法检查通过。
- tests/test_guard_run.py 七项检查通过：正常/非零退出、完成后启动器挂起、rank 完成
  但无 finalized、rank 未全部退出时运行时限、继承日志描述符的残留子进程；提交节点监督仅回收已有完成凭证的运行作业，
  不回收没有完成凭证的计算中作业。
- 614522：8 步真实单 GPU 运行正常退出后调度仍为 RUNNING；提交节点监督器自动
  执行 terminate_after_exit，终态 FAILED，资源释放。
- 614523：验证正式 submit_guarded.sh 自动启动并脱离提交终端的监督器，应用与启动器均为 0，完成凭证发布后监督器自动
  终止残留分配，09:59:04 终态 FAILED、TASK_RUNNING_CNT=0。

日志及退出凭证分别保存在 runs/*job614521、runs/*job614522、runs/*job614523 和
logs/submit/<job>-supervisor.json。被监督器回收的 FAILED 不等同于数值失败。

## 10:54 登录配置修正及原始入口验证

管理员指出用户登录配置的后台进程。现场确认 `~/.bash_profile` 在非交互式
登录条件下无条件后台启动 `lightread-connector resume`；仓库启动器的 MPI rank
使用 `bash -lc`，会读取此文件。即使标准输入输出重定向到 /dev/null，后台进程
也未必脱离批处理作业的进程归属。`.bashrc` 虽有非交互式 return，不能阻止
`.bash_profile` 在其返回后再次启动后台连接。

用户授权修改后，已移除 `.bash_profile` 中的非交互式恢复段；交互终端的
`.bashrc` 恢复功能保留。对 `.cargo/env` 与 `.local/bin/env` 的读取增加可读性检查。
配置备份：`/home/share/iosoeqkp/home/Liuzhaohui/shell-startup-backups/20261016_105414_no_batch_lightread/`，
包含原 `.bash_profile`、`.bashrc`、`.bash_logout`；`.bash_logout` 未修改。
`bash -n` 通过，新 `bash -lc` 正常返回且 djob/dsub 均能找到。

作业 **614533** 使用 613417 原可执行文件和旧提交脚本（保留 tee），不启用
新增 guard/提交节点监督，32³ 单层 OSI 运行 8 步；只设 120 秒调度时限作安全保护。
10:54:21 开始、10:54:33 自行 SUCCEEDED，运行任务数为 0，未触发时限或人工回收。
日志位于 `runs/20261016_105421_job614533/run.log`。

这次验证强烈支持登录配置中的非交互式后台连接是故障原因，不支持仅归因于
数值程序、MPI 或 tee。原挂起作业的残留进程未被捕获，不能补写为已直接观察到
LightRead 留在原作业 cgroup。退出防护继续保留；当前验证覆盖单节点单 rank，
不据此承诺所有平台故障都可由本次修改解决。
