# MPI rank 启动不再读取个人登录配置

## 目的与影响范围

避免计算节点执行个人登录配置中的后台连接程序。修改公共 `scripts/submit.sh`，
影响调用该脚本的算例入口；未批量修改各算例独立维护的历史实验脚本。

## 主要改动

- 保留现有 module 加载、环境传递、GPU 绑定、输入参数拆分和 exec 启动。
- 将 rank 的 `bash -lc` 改为 `env -u BASH_ENV bash --noprofile --norc -c`，
  避免读取个人登录文件及非交互式启动文件。
- 修正脚本首行为有效的 Bash shebang；不改数值路径。

## 验证

- 公共脚本 `bash -n`、`git diff --check` 通过。
- BOX3D_OSI 作业 614534：通过历史续跑入口调用修改后的公共脚本，使用原
  613417 可执行文件，从初始状态执行 OSI 8 步，32³、单层、单 MPI rank、单 GPU。
  未使用额外退出监督器；120 秒调度超时未触发。
- 2026-10-16 11:07:15 至 11:07:24，自行 SUCCEEDED，TASK_RUNNING_CNT=0，
  日志包含 STEP 8 和 AMReX finalized。
- 日志位于 `projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261016_110715_job614534/run.log`。
  本次验证环境初始化和正常退出，不作为数值等价或多节点可用性的证明。
