# 最高 level 2 四 GPU 续跑与显存采样

- 目的：双 GPU 的 job615925 在 step=256448 重网格时显存耗尽后，按用户指令用 4 GPU 重试。未修改数值判据或层级设置。
- 作业：[job615927](../../runs/20261020_195907_job615927/)，whshare-agent-195，单节点 4 GPU / 4 MPI ranks，CPU=128、内存=196608 MiB。复用临时显存采样入口，以命令行 `-R 'cpu=128;mem=196608;gpu=4'` 覆盖其默认双 GPU 资源头；实际调度记录保存在运行目录 `scheduler-start.txt`。
- 数值配置：原 job615364 的 `chk00256000` 只读重启，step=256000 至最多 288000；Re=3200，基础网格 128³，域长 1，最高 level 2，state_error_6、n_start=-2、n_inc=1、编译期 max_ref_level=3 均保持。每 32 步 regrid，每 3200 步输出 PlotFile/checkpoint。
- 收敛：保持开启，level 0 速度相对 L2 变化每 3200 步检查，容差 1e-4、连续 3 次满足则提前停止；趋势间隔 1000 步。精确输入以运行目录 inputs 和 overrides.txt 为准。
- 构建：`GEN_CCDB=0 MAKE_J=8 ./scripts/compile.sh --no-submit` 成功，目标最新；日志 `logs/compile/compile-20261020T195850.log`。app SHA256 为 `2af4439d8e87ed56419be0e0e12e3dac101cecdb0e430a39a302a97537b1bd05`，与双卡作业一致。
- 采样：[CSV](../../runs/20261020_195907_job615927/gpu-memory.csv) 每 200 ms 记录各卡设备总显存占用，包含 CUDA 上下文和 allocator 池，不能等同活跃数组大小；可能漏掉短时峰值。[汇总 JSON](../../runs/20261020_195907_job615927/gpu-memory-summary.json) 保存中途峰值，监督器确认终态后独立汇总进程自动重算全程峰值并退出。
- 现场验证：日志确认 4 MPI processes、从 step=256000 重启。截至 step=256477，已经通过双卡失败的 step=256448 重网格，尚未出现显存错误，作业 RUNNING。20:01:51 的采样峰值为 GPU0=30709、GPU1=30711、GPU2=30717、GPU3=30719 MiB（均约 30.0 GiB，每卡总量 40960 MiB）。这里只证明当前阶段通过，不代表完成 32000 步、最终显存峰值或收敛。
- 留证与退出：app 副本、源码归档、工作区差异、输入覆盖和采样工具副本均留在运行目录。继续使用正式 guard_run.py 与 watch_job.py 的异常退出回收机制。提交脚本 bash -n、记录路径链接和 diff 检查通过；不提交用户已有源码与 Re 修改。
