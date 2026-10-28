# 启用粒子单精度

目的：按用户选择，启用 AMReX 的 `USE_SINGLE_PRECISION_PARTICLES = TRUE`，
解决 FLOAT 场配置下 `force_wall_model` 法向量列表初始化的 double 到 float 窄化诊断。

范围与改动：仅 BOX3D_OSI。复用 AMReX 原生粒子精度开关，不替换 `ParticleReal` 类型，
不修改 AMReX 或数值核。标记点和辅助点的坐标、浮点属性改为 float；
即使显式选择 DOUBLE 场，粒子仍为 float。README 同步该独立精度选择。
编译入口复用已有数据库有效性检查，增加粒子单精度宏检查，避免合并旧精度记录。
首次增量构建发现旧 `AMReX_Geometry.o` 仍提供 double 粒子接口，导致 float 接口链接失败。
复用 AMReX 的 `TMP_BUILD_DIR` 机制，按粒子精度隔离对象目录后重新构建，
避免默认目录不区分粒子精度而混用 ABI；不删除历史对象或程序。

验证材料保留在 `runs/temporary/single_precision_particles/`（临时验证，保留日志和复现输入）。
`scripts/temporary/single_precision_particles/check.sh` 为本次临时检查入口，
使用生产 `Kernels.H`，对 17 个位置检查均匀场下 IBM 插值力、扩散总力和辅助点压力插值。
粒子均为 4 字节：FLOAT 场最大尺度化误差 `2.6834e-06`，DOUBLE 场为 `3.8147e-08`，
均通过 `2e-5` 判据。这是局部 CPU 解析解检查，不是长时间 GPU IBM 耦合精度验收，
也未验证壁面模型、粒子归约或 checkpoint 跨精度重启。

构建使用当前工作区，包括用户已有的 `src/LagrangeParticleContainer.cpp` 修改；
该文件不纳入本次提交。

编辑器验证：工作区指定的 clangd 18.1.8 使用含粒子单精度宏的编译数据库解析
`Kernels.H`，语义诊断为 0，原来的两条窄化诊断消失。
`--check` 另有 79 条 `DefineOutline` 无合适实现文件的重构功能检查失败，
不作为语义或数值验证通过的依据。Shell 与嵌入 Python 语法检查通过。

隔离目录后的三维 FLOAT CUDA+MPI 完整构建通过（exit=0），生成
`main3d.gnu.FLOAT.TPROF.MPI.CUDA.ex`，日志为 `build_isolated.log`。
最终编译数据库完整覆盖 9 个算例源文件，全部含 FLOAT 场和单精度粒子宏，
包含路径均指向 `tmp_build_dir/particles_float/`。未提交 GPU 生产作业。
