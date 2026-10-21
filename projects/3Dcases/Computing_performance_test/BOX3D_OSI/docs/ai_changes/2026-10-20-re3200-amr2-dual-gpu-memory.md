# 最高 level 2 双 GPU 续跑与显存采样

- 目的：按用户要求，保留最高 level 2 与当前加密判据，用双 GPU 再续跑，并观察显存峰值。
- 配置：从 job615364 的 `chk00256000`（step=256000）续跑，最多到 step=288000；Re=3200、基础网格 128³、域长 1、OSI、D3Q27 双精度；`state_error_6` 的 n_start=-2、n_inc=1、编译期 max_ref_level=3 保持不变。regrid 每 32 步，PlotFile/checkpoint 每 3200 步。
- 收敛：启用 level 0 速度相对 L2 变化判定，每 3200 步检查，容差 1e-4，连续 3 次满足则提前停止；趋势间隔 1000 步。
- 构建：`GEN_CCDB=0 MAKE_J=8 ./scripts/compile.sh --no-submit` 成功，目标最新；日志 `logs/compile/compile-20261020T194829.log`。app SHA256 为 `2af4439d8e87ed56419be0e0e12e3dac101cecdb0e430a39a302a97537b1bd05`。
- 作业：[job615925](../../runs/20261020_194926_job615925/)，whshare-agent-174，2 GPU / 2 MPI ranks，CPU=64、内存=98304 MiB。复用正式 submit.sh、guard_run.py 与 watch_job.py；新增临时提交包装器只负责采样和退出清理，不改求解器。源码归档、差异、独立 app、输入覆盖和临时工具副本均保存于运行目录。
- 采样：当时每 200 ms 记录 GPU 编号、UUID、时间、memory.used 与 memory.total（MiB），包含上下文和 allocator 保留池，可能漏掉瞬时峰值。2026-10-21 收尾检查发现 gpu-memory.csv 为失效软链，原始 CSV 缺失；下述汇总仍在，但已不能从原始采样重算峰值。
- 汇总：[JSON](../../runs/20261020_194926_job615925/gpu-memory-summary.json) 包含每卡最大采样占用及首次达到该值的时间、样本数与最后占用。独立轻量汇总进程等待调度监督器的终态凭证，结束后重算最终 JSON 并退出；可用运行目录内 `summarize_restart_memory.py <运行目录>` 随时刷新中途汇总。
- 终态：在 step=256448 重建 level 2 时显存不足，exit_status.json 记录退出码 6，已退出。rank 0/1 失败申请分别约 10.24/10.66 GiB；最终设备采样峰值为 38847/39501 MiB（37.94/38.58 GiB）。失败的申请不计入占用峰值，不能认为只差几百 MiB；未到正式收敛采样点。
- 首次提交 job615924 因临时入口缺执行权限而立即失败，未启动应用；补可执行位后提交 job615925。失败日志保留。Shell 语法、Python AST 和 diff 检查通过。
- 生命周期：两个临时工具的作业内依赖已结束；运行目录工具副本与汇总保留，原始采样缺失如上。工作区入口的处置见后续收尾记录，不再视为运行中依赖。
