# 用MPI版ParaView替换远端串行环境

状态：已安装并完成节点侧MPI功能验收；旧串行应用前缀已删除。

用户授权：安装MPI版并删除原串行版。只替换仓库私有应用，不修改系统软件、启动文件或求解器。
新前缀为 `.tools/paraview-mpi/env`，统一服务GUI与pvbatch；旧前缀为 `.tools/paraview-batch/env`。
默认GUI使用4进程，可通过ServerProcesses调整；批处理仍默认单rank，可用MPI应用独立启动。

ParaView源码6.2.0官方归档SHA256：
`373afd566ef5f6c650c3acf4a5d4f43ad75975537f7fe225cb53ed20fe41cd20`。
依赖用88包显式SHA256锁文件固定，包括Python3.12、MPICH、GCC/G++、Mesa/EGL。
开启MPI/Python/EGL，关闭Qt、Fortran和Viskores；源码/目标文件在编译节点本地盘，
安装与日志在共享前缀；构建配置保留在build-config.cmake.txt。安装器保持原文件名兼容调用者。

安装器不重复重链匹配依赖，并检查四个应用入口、MPI构建标志及pvpython控制器；使用编译节点本地互斥锁。
源代码SHA256与依赖锁均通过校验，CMake配置确认MPI、Python、EGL开启，Qt关闭； ParaView 6.2.0成功安装。

验收（whshare-agent-1）：
- `pvserver --version`报告6.2.0；配套MPICH为4.3.2。
- `mpiexec -n 2 pvbatch ... test_vorticity_pipeline.py`的5项测试通过，确认MPI控制器进程数为2；真实AMReX输入旋度管线也成功生成。
- 1进程与既有Re=3200、step=256000中心线CSV逐项一致（1025点、0无效点、最大差0）；4进程同样输出1025个有效点且坐标一致，但速度最大差为`5.7186e-5`，不视为与串行数值等价，需进一步定位并行过滤器差异。
- 4进程pvserver被pvpython客户端连接，远端会话报告4个数据服务进程；实际AMReX数据上的涡量切片生成640x480截图，最大涡量幅值13.12515。
- 安装器重复运行跳过依赖重装，并成功探测MPI控制器。shell语法检查通过。

旧`.tools/paraview-batch/env`与其mamba/安装器缓存已删除；历史安装日志、求解计划和GUI smoke证据保留。新的构建日志、源码归档、依赖缓存、构建配置和验收产物保存在`.tools/paraview-mpi/`。Windows ParaView客户端的实际GUI连接未在本次会话验证；用户需通过更新后的连接脚本启动新服务连接。MPI多进程功能已验证，但不代表当前中心线过滤结果已数值等价，也未测得性能提速。
