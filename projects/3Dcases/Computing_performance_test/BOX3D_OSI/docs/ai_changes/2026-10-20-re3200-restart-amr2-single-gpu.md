# 最高 level 2 的单 GPU 续跑尝试

- 目的：按用户新指令，将 `config/inputs` 中 `amr.max_level` 从 3 改为 2，从 job615364 的 `chk00256000` 用单 GPU 再尝试续跑最多 32000 步。
- 范围：只调整运行时最高层号，保留用户现有 `state_error_6`、`n_start=-2`、`n_inc=1` 和 Re=3200；未修改编译期 `max_ref_level=3`，所以层级映射的涡量阈值保持原值（level 0/1 分别为 0.03125/0.0625）。这不是同时把映射中的最高层号改成 2。
- 运行：[job615919](../../runs/20261020_192716_job615919/)，节点 whshare-agent-67，1 GPU / 1 MPI rank，CPU=32、内存=49152 MiB。从 step=256000 续跑，max_step=288000；regrid_int=32，PlotFile 和 checkpoint 间隔均为 3200，保留最新本地 checkpoint；源 checkpoint 仅通过软链读取。
- 收敛：开启 level 0 速度相对 L2 变化判定，每 3200 步比较，容差 1e-4，连续 3 次满足则停止；每 1000 步输出趋势。重启时重新初始化比较场，第一条正式检查应在 step=259200。
- 验证：`GEN_CCDB=0 MAKE_J=8 ./scripts/compile.sh --no-submit` 成功，make 确认目标最新，日志 `logs/compile/compile-20261020T192657.log`。提交脚本 `bash -n` 和 CRLF 兼容 diff 检查通过。独立 app、SHA256、源码归档、source.diff、inputs 和 overrides 已保存；app SHA256 为 `2af4439d8e87ed56419be0e0e12e3dac101cecdb0e430a39a302a97537b1bd05`。
- 结果：成功重启并推进 step=256001–256063，在 step=256064 第二次 regrid 创建 level 2 的新分布时 GPU 显存耗尽。设备总显存 40339 MB、剩余 285 MB，申请 376358400 bytes（358 MiB）失败，日志记录 `cudaMalloc returned 2: out of memory` 和 SIGABRT。调度监督器确认 FAILED，随后查询无运行作业。
- 边界：32000 步续跑未完成，尚未到任何收敛采样点。该结果说明当前映射阈值下，最高 level 2 的此次单卡重网格峰值也超过容量；不能推断稳态网格大小或数值收敛。没有自动扩卡、调高阈值或删除历史结果。inputs 中最高层号保持为用户要求的 2；用户已有 Re 和源码改动不并入本记录提交。
