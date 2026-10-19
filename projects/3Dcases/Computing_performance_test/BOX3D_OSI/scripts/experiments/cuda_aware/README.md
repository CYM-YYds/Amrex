# CUDA-aware MPI 未验收实验

三件套使用独立的 OpenMPI 构建和 `main3d_cuda_aware3d.gnu.TPROF.MPI.CUDA.ex`，
与生产 HMPI/host-staging 流程分开保留。独立 app 当前存在，不证明其对应最新源码或环境已通过验收。

| 文件 | 职责 |
|---|---|
| [compile_cuda_aware.sh](compile_cuda_aware.sh) | 在编译节点使用 OpenMPI、独立构建目录与 EBASE 构建，不覆盖生产 app |
| [submit_osi_mpi_device_correctness.sh](submit_osi_mpi_device_correctness.sh) | 两rank、周期固定网格的64步锁步正确性实验 |
| [submit_osi_mpi_device_perf.sh](submit_osi_mpi_device_perf.sh) | 同一环境比较 A-B、host-staging 与 device-direct 的1000步性能 |

入口从算例目录调用：`./scripts/experiments/cuda_aware/compile_cuda_aware.sh`；提交实验时使用
`dsub -s scripts/experiments/cuda_aware/<提交脚本名>`。这些命令仅说明入口，本次维护未执行构建或提交。

保留 OpenMPI4.1.5/CUDA11.6 模块、CUDA12.8 编译器、UCX运行库和显式 OPAL_PREFIX 的原有选择。
解除未验收状态需要核实 app 的代码版本与环境兼容性，再完成该 MPI 环境的正确性检查，
最后才解释性能结果；生产 host-staging 的历史通过不能外推到 device-direct。
本次只检查迁移后的路径和模拟启动参数，日常入口见[算例 README](../../../README.md)。
