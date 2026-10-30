# 均匀网格与t=1000实验知识收尾

范围：BOX3D_OSI本轮均匀推进、窗口计时、正式运行与论文参考图；不做全仓内容审计、结构重构或新增数值实验。

| 事实面 | 状态 | 证据与处置 |
|---|---|---|
| 代码 | verified-current | 当前src与job621699冻结src逐字节一致；每步累计、按report_int窗口平均，独立采样参数无src引用，支持范围以README为准 |
| 运行态 | verified-current | 调度器、监督器、exit_status均成功；最终checkpoint为step256000、t=1000、lev0；当前程序与冻结程序SHA256一致；图和参考误差文件可读 |
| 文档 | changed-and-verified | 更新当前状态、架构计时例外、结果导航和现成CSV快速重绘；175个本地链接/锚点检查通过（含根规则链接） |
| 规则 | verified-current | 根AGENTS.md为CLAUDE.md有效软链；实际上级链无额外覆盖，全局AGENTS为空；不修改项目或全局规则 |
| 记忆 | out-of-scope | 平台生成记忆仅只读检查（generated-read-only）；用户未明确要求写入，不修改生成文件或建立平行记忆 |
| 工作区 | changed-and-verified | 单工作树/Amrex分支；用户已有修改和结果保留；汇报后获用户明确确认，仅删除已核对同哈希的重复构建程序，不声明整个工作区干净 |

## 现役答案

- 使用与计时：[README](../../README.md#实际推进调用链)；运行与验证：[当前状态](../current_status.md)。
- t=1000正式结果及论文点来源：[运行记录](2026-10-30-uniform-256-t1000.md)。
- 已有CSV只加参考或改样式时直接绘图，不重复提取、启动ParaView或重复检索未变化来源；命令见[后处理说明](../../data_post_processing/README.md#复用现成csv快速重绘)。本次不重新出图或重跑求解器。

## 保留现场与清理结果

机械盘点覆盖190份Markdown/MDX（排除依赖/构建目录），阅读与本轮相关现役文档；带日期历史记录保留各自验证范围，不改写为当前验收。

- 清理候选：runs/temporary/uniform_kernels/round1/build/main3d.gnu.FLOAT.TPROF.MPI.CUDA.ex，117.35 MiB，与同目录上级nomask.ex的SHA256相同。用户在完整汇报后回复“确认清理”；2026-10-30再次核对SHA256相同后仅删除该副本，保留上级nomask.ex且哈希未变。
- prepare_uniform_kernels.py、build_uniform_kernels.sh、analyze_uniform_kernels.py、uniform_dispatch.sh及analyze_uniform_dispatch.py仍用于冻结实验复现，不因“临时”直接删除。
- inputs、MLUPS记录.md、AmrCoreLBM_amr.cpp的既有修改不纳入提交；未跟踪logs/含实际构建/监督凭证，范围外2DVorticity.py来源不在本次裁决范围，均保留。
- 未消除的warning：旧comm等字段在减少同步路径为未测量零值，不能解释成没有通信；GPU/CPU区间不可相加；数字化论文点不是原始真解数据。现役文档均已说明。
- out-of-scope：旧AMR/Q9、多节点、GPU-aware MPI与运动IBM等数值待验项，其他算例、全局config.toml、PR/远端发布及GUI管线。本次无服务部署，不声明deployed或整个工作区干净。

验证仅按文档变更执行：链接/路径、源码与运行证据交叉核对、git diff空白检查。未新增构建或数值测试。首次汇报前未删除材料；获用户确认后仅删除上述123046816字节副本，没有删除分支、工作树、源码、脚本、输入、日志、图或checkpoint。清理后git status中的用户既有5项保持不变，无新增待清理对象。
