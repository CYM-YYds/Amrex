# 二三维开发算例的公共构建与 MPI 启动参数

目的：允许 BOX3D_OSI_dev 在同一入口选择 DIM=2/3，保留其他算例默认构建和运行行为。

主要改动：公共 compile.sh 接受可选 AMREX_BUILD_DIM，并校验、传到远端及普通/完整
重建的 make 命令；产物提示优先匹配选择维度。公共 submit.sh 接受可选
AMREX_EXECUTABLE，默认仍使用原三维程序，MPI 进程接收该变量后按原 GPU 绑定方式启动。
算例入口负责 DIM 默认值、二维输入和当前维度的完整编译数据库，不新增共享模型状态。
日志摘要改用完整读取管道后截取的 sed，避免 grep/head 在 pipefail 下导致成功构建返回 141。

验证：Shell 语法检查、非法 DIM 拒绝、二维/三维产物选择检查通过；BOX3D_OSI_dev
二三维 CUDA+MPI 编译、维度切换通过；当前数据库覆盖九个三维算例源文件，全部
条目为 DIM=3；无改动重编返回 0 并复用数据库。作业 613523 经正式算例提交入口、
公共 MPI 启动脚本实际选择 inputs.2d/main2d，单 rank 两步成功且末态有限。数值验证与范围限制见
[算例记录](../../projects/3Dcases/Computing_performance_test/BOX3D_OSI_dev/docs/ai_changes/2026-10-15-dimension-port.md)。
未启动其他算例的生产作业，未改变其默认可执行文件、输入或物理参数。
