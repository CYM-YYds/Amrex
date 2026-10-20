# 最高 level 2 双 GPU 续跑与显存采样

- 目的：按用户要求，保留最高 level 2 与当前加密判据，用双 GPU 再续跑，并观察显存峰值。
- 配置：从 job615364 的 `chk00256000`（step=256000）续跑，最多到 step=288000；Re=3200、基础网格 128³、域长 1、OSI、D3Q27 双精度；`state_error_6` 的 n_start=-2、n_inc=1、编译期 max_ref_level=3 保持不变。regrid 每 32 步，PlotFile/checkpoint 每 3200 步。
- 收敛：启用 level 0 速度相对 L2 变化判定，每 3200 步检查，容差 1e-4，连续 3 次满足则提前停止；趋势间隔 1000 步。
- 构建：`GEN_CCDB=0 MAKE_J=8 ./scripts/compile.sh --no-submit` 成功，目标最新；日志 `logs/compile/compile-20261020T194829.log`。app SHA256 为 `2af4439d8e87ed56419be0e0e12e3dac101cecdb0e430a39a302a97537b1bd05`。
- 作业：[job615925](../../runs/20261020_194926_job615925/)，whshare-agent-174，2 GPU / 2 MPI ranks，CPU=64、内存=98304 MiB。复用正式 submit.sh、guard_run.py 与 watch_job.py；新增临时提交包装器只负责采样和退出清理，不改求解器。源码归档、差异、独立 app、输入覆盖和临时工具副本均保存于运行目录。
- 采样：[原始 CSV](../../runs/20261020_194926_job615925/gpu-memory.csv) 每 200 ms 记录 GPU 编号、UUID、时间、memory.used 与 memory.total（MiB）；采样是设备总占用，包含上下文和 allocator 保留池，不等于当前活跃数组大小，也可能漏掉两次采样间的瞬时峰值。此单节点记录中两张卡均由本作业分配。
- 汇总：[JSON](../../runs/20261020_194926_job615925/gpu-memory-summary.json) 包含每卡最大采样占用及首次达到该值的时间、样本数与最后占用。独立轻量汇总进程等待调度监督器的终态凭证，结束后重算最终 JSON 并退出；可用运行目录内 `summarize_restart_memory.py <运行目录>` 随时刷新中途汇总。
- 启动验证：截至 step=256169，已通过此前单卡失败的 step=256064，并经过后续重网格；调度状态 RUNNING。19:50:52 的中途采样峰值为 GPU0=38521 MiB（37.62 GiB）、GPU1=38925 MiB（38.01 GiB），每卡总量 40960 MiB。该数值不是全程最终峰值；尚未到正式收敛检查点，不证明完成或收敛。
- 首次提交 job615924 因临时入口缺执行权限而立即失败，未启动应用；补可执行位后提交 job615925。失败日志保留。Shell 语法、Python AST 和 diff 检查通过。
- 生命周期：`scripts/temporary/submit_restart_memory.sh` 与 `summarize_restart_memory.py` 为本任务临时工具；当前作业仍依赖采样入口，不清理。运行目录的副本、原始采样和汇总作为复现证据保留。
