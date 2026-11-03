# Roofline 小测试结果（2026-11-03）

三轮短基线完成；真实 Roofline 采集被 GPU 性能计数器权限阻止。当前只能报告计时和逻辑读写带宽估算，不能报告实测 DRAM 带宽利用率、执行 FLOP/s 或 kernel 在 Roofline 上的位置。

## 环境与输入

- job624743，whshare-agent-82，NVIDIA A100-PCIE-40GB，单 GPU/单 MPI rank；驱动 535.104.12，Nsight Compute 2025.1.0.0。
- 当前提交基线 `2a1f539cce8a1dcd7b17b1f5ad26db729310cda8`；冻结程序 SHA256 `a098bb75c1ec334b211484cb9be5de2e7ac0c298303b012127afdb669753fb3c`。当前构建入口成功，make 报目标已为最新，没有强制全量重编。输入包含用户已有注释修改，随本轮快照保留但不提交用户改动。
- 256³、L0、单 Fab、D3Q27、FLOAT（日志确认 Real=4 bytes）、OSI、Re≈1000；关闭 regrid 和收敛停止，开启均匀网格无 mask 碰撞及减少同步路径。
- 从历史 step256000 checkpoint 各推进 100 步；排除每轮最初 20 步，分析区间为 step256021–256100。12 个等长窗口，共 240 个分析步。基线含程序自动 CUDA 事件采样，短窗口不足以证明长期性能稳定。
- `perf(s).total` 是推进统计时间，不含初始化与窗口外输出；`collide_sum` 为 CUDA 事件记录的碰撞 GPU 时间，本轮单 Fab，避免将 host 提交时间当成 kernel 时间。

## 测量与估算

| 轮次 | 推进 ms/step | 碰撞 GPU ms/step | 边界 GPU ms/step | MLUPS_total | 逻辑有效带宽 GB/s |
|---|---:|---:|---:|---:|---:|
| 1 | 4.651 | 3.666 | 0.955 | 3607.5 | 988.6 |
| 2 | 4.616 | 3.633 | 0.955 | 3634.6 | 997.5 |
| 3 | 4.622 | 3.636 | 0.953 | 3629.6 | 996.7 |

等长步数平均推进 4.630 ms/step，碰撞 3.645 ms/step；累计更新量除以累计推进时间为 3623.8 MLUPS。碰撞 GPU 时间约为推进时间的 78.7%，这里只表示该诊断窗口中的耗时比例。

逻辑流量根据冻结的 `src/Kernels.H` 中 `collide_bgk_register_impl` 的 DDF 读写循环计算：每 cell 读 27 个 FLOAT、写 27 个 FLOAT，即 `27×4×2=216 bytes/cell`。`src/AmrCoreLBM_advance.cpp` 的碰撞域在本轮非周期、无细层、单 Fab 条件下恰为 256³ valid cells；日志逐窗口确认每 20 次调用发射 335544320 cells。因此每次碰撞的逻辑流量为 `256³×216=3623878656 bytes`，用此流量除以碰撞 GPU 时间，三轮累计得到 **994.2 GB/s（十进制）**。

该数字是算法逻辑读写量/时间，并非硬件测得的 HBM 流量。它不能反映 cache 命中、事务放大、写回、额外局部内存流量或整数寻址指令开销；不据此计算真实显存利用率，也不据此断言 memory-bound。未统计执行 FLOPs，不生成伪 Roofline 点。

## 采集失败与验证边界

- `job624743/status.tsv`：三次 baseline 均退出 0，`ncu_collision=1`。调度器最终 FAILED 对应 profiler 失败，并不表示前三次基线失败。
- `job624743/ncu_collision/ncu.log` 在第一步报告 `ERR_NVGPUCTRPERM`；`job624743/driver-params.txt` 确认 `RmProfilingAdminOnly: 1`。没有生成 `.ncu-rep`，因此“跳过 16 次匹配发射后采集一个 kernel”的实际匹配和预热采集位置尚未经验证。
- 原始错误给出的管理员说明：[NVIDIA 性能计数器权限说明](https://developer.nvidia.com/ERR_NVGPUCTRPERM)。下一步需管理员允许普通用户采集，或在有授权的节点重试；本轮没有尝试提权或修改驱动设置。
- 三次最终状态检查完成，输出的 DDF/rho 极值和速度范数均有限且文本一致（rho_min=0.995421946、rho_max=1.006722569、velocity_linf=0.05000006035）。这仅是短测健康检查，不是逐值数值等价或物理收敛证明。
- 第三轮保留最终 `chk00256100`、速度 `plt_256100`、涡量 `plt_vort_256100`；其 Header 均存在，原始重启 checkpoint 保持只读链接。
- 首次 job624742 因新脚本未设执行位，在启动应用前失败；补上执行位后提交 job624743，失败提交日志保留。
- 未修改求解器或正式输入，未作历史跨节点加速对比。本轮是均匀网格方法验证，不代表 AMR 插值/平均/通信的带宽或多 GPU 表现。

## 复现与保留

在算例目录通过 `dsub -s scripts/temporary/roofline_smoke.sh` 提交；再次提交前如需新源码，重新构建并在新一轮目录冻结，当前脚本绑定本轮 round1 快照，不覆盖既有 job 目录。每次实际 MPI/profiler 命令见对应 `command.txt`。

在仓库根运行：

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/analyze_roofline_smoke.py projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/roofline_smoke/round1/job624743
```

可重新生成 [summary.json](summary.json)。程序、源码/输入快照、构建日志、SHA256、完整原始日志、命令、场和 checkpoint 均保留于本目录。两个临时脚本仍为本结果的复现依赖，本轮不删除。
