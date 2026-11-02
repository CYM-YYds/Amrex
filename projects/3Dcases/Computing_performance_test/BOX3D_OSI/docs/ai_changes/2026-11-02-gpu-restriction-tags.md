# CUDA restriction 任务在 GPU 展开

目的：减少完整平均下传及界面缓存重建时，CPU 逐 block 生成并上传 `RestrictionBlockTag` 的成本。范围仅 BOX3D_OSI。

## 实现与生命周期

扩展已有 `BuildRestrictionWork`，保持 `RestrictionBlockTag` 格式、调用者、相位槽位和 restriction 消费内核不变。CPU 每个区域保存一个原标签模板和 block 起点；GPU 单 kernel 二分查找所属区域、展开完整标签表。正式 CUDA 路径移除原 CPU 逐 block 建表，不新增运行参数，也不引入第二套持久状态。实验开关和专用计时只留在冻结候选中。

新增匿名命名空间 helper 封装局部描述上传与展开：描述来源仍是本次调用的 result/fine 布局，设备缓冲在建表结束前同步并释放；输出 work 沿用调用者原生命周期。独立 helper 同时满足 NVCC 扩展设备 lambda 对成员访问和捕获类型的约束。非 CUDA 分支不变。

## 冻结比较

程序 SHA256：`b46d1a98416c9b2b3831c310c4a660d5397308998083467b29741158dce28f0f`。
三维 FLOAT、AMReX 26.06、sm80、HMPI host staging，checkpoint 为 `runs/a5_B_optimized_128000/job622137/B/chk00128000`。每轮执行 640 步，丢弃首 160 步，统计后 480 步/15 次 regrid；同程序 CPU/GPU 开关，ABBAABBA 各四轮。`perf(s).total` 包含重网格、不含初始化，关闭嵌套细分同步。两 rank 按每窗口较慢 rank 计时后累加，不能加总 rank 时间。

| 作业 | 硬件范围 | CPU 建表平均 | GPU 建表平均 | 耗时降低 |
| --- | --- | ---: | ---: | ---: |
| 623302 | agent-96，1 张 A100 PCIe 40GB | 21.066250 s | 20.868145 s | 0.9404% |
| 623333 | agent-195，同节点 2 张 A100 PCIe 40GB、2 rank | 32.286574 s | 32.154257 s | 0.4098% |

双 GPU CPU 四轮：32.217133、32.386799、32.250670、32.291696 s；GPU 四轮：32.143241、32.261964、32.096930、32.114893 s。样本标准差分别 0.073445、0.074292 s。四对相邻 A/B 均未退化，但样本范围重叠，不能据此宣称稳定的多 GPU 加速。用户以多 GPU 未下降为合入条件，本次测量满足该条件；这些短测不能外推为完整生产运行或跨节点扩展性结论，不跨节点比较 1/2 GPU 的因果性能。

两组报告均核对逐 rank、逐层 calls/launch_cells 及 AMR 工作计数一致。192 步周期动态重网格回归中，CPU/GPU 的 35 个 valid Fab、3,870,720 个 DDF 数值全部有限、逐位一致，各层 valid/active 范数均为 0。该最终布局到 level 1，不能自动当作所有 level 2 或长期物理收敛验证。

双 GPU 初次作业 623331 在两组回归成功后，分析器误将仅由 IO rank 输出的全局 finalize 信息要求于所有 rank，故计时前停止。修正后作业 623333 全部成功；623331 不纳入计时统计，原始记录保留。

## 正式集成验证

正式程序已完成三维 FLOAT CUDA+MPI 编译（GCC 11.3、CUDA 12.8、HMPI 1.2.0、sm80）。标准脚本的远程编译连接被关闭，未进入编译；随后在本机加载相同工具链，`make -j8 CUDA_ARCH=80 DIM=3 PRECISION=FLOAT` 成功退出 0，完整日志与命令已冻结。正式程序 SHA256：`a098bb75c1ec334b211484cb9be5de2e7ac0c298303b012127afdb669753fb3c`。

集成回归作业 623351（双 GPU、2 rank、192 步）正常退出，复用 623333 的 CPU checkpoint 作只读基线。35 个 valid Fab、3,870,720 个 DDF 值全部有限、逐位一致，valid/active 各层范数及全局误差均为 0。正式源码只替换建表方式，数值消费路径不变；依据已有冻结性能证据与该回归完成集成，不重复整套性能矩阵。本轮不声称二维、DOUBLE、多节点、所有 AMR 层级或长期物理收敛通过验收。

Shell `bash -n`、Python 语法解析、文档链接与 `git diff --check` 均通过。

## 复现材料

- `runs/temporary/restriction_gpu_tags/round1/job623302/`：单 GPU 原始日志、checkpoint 与 `report.json`。
- `runs/temporary/restriction_gpu_tags/round2/job623333/`：双 GPU 各 rank 日志、checkpoint、输入、程序及 `report.json`。
- `runs/temporary/restriction_gpu_tags/integration/`：集成前程序与用户差异备份、构建日志、正式程序及回归。
- [分析入口](../../scripts/temporary/analyze_restriction_gpu_tags.py)：在仓库根用 `.venv/bin/python <脚本> <job目录>`；仅核对回归时加 `--regression-only`。
- [提交入口](../../scripts/temporary/submit_restriction_gpu_tags.sh)：算例目录设置 `CCS_CLI_HOME=/opt/batch/cli REGRID_CASE_DIR="$PWD"` 后用 `dsub -s scripts/temporary/submit_restriction_gpu_tags.sh`；`RESTRICTION_MODE=integrated` 选择集成回归，默认 benchmark 使用 round2 冻结候选。

临时入口保留为本次结果复现材料。用户已有 `AmrCoreLBM_advance.cpp` 修改保持原样，不纳入本提交。
