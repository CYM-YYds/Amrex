# 宏观量与本地ghost复制默认范围统一

目的：按用户要求，将ComputeMacroLevel和FillBoundaryOsi本地复制的坐标复用也统一至CUDA FLOAT二维/三维、单/多rank，与碰撞的适用范围一致。

AmrCoreLBM.cpp和AmrCoreLBM_osi.cpp仅删除坐标复用发射处的AMREX_SPACEDIM==3与NProcs()==1限制，复用现有compute_macro_osi<true>及LaunchLocalGhostCopy<true>；CPU/DOUBLE沿用原寻址。未增加抽象、参数、持久状态或替代公式。线程映射、消息安排、跨stream依赖、phase及Fab环不变。FillBoundaryOsi内区分本地与MPI执行的NProcs分支仍然必要，没有移除。平均/插值及MPI打包/解包、ParallelCopyOsi不变。

沿用collision_amr提交入口（COORD_STAGE_TEST=other-coordinates）。复用前轮已通过的二维和三维碰撞缓存，二维使用修正后的followup2d_build对象，避免混入前轮未触发重编的旧对象。切换构建时touch受影响源，分别构建DIM=3/2 FLOAT。仅两组2GPU回归：三维真实三层32粗步、二维周期动态两层80步。复用前轮命令和独立checkpoint，检查各层valid/active L1/L2/Linf，并对全部速度/涡量Cell_D数据逐字节校验。

不重跑既有性能矩阵；本轮仅确认扩大默认后的数值及输出一致性，不宣称宏观量/本地复制多GPU收益，也不把碰撞核的多GPU结果当作这两处的加速比。未新验收DOUBLE、CPU、多节点或长期物理精度。

job621982 SUCCEEDED：两种维度的CUDA FLOAT编译通过；2GPU下三维三层、二维动态两层的各层valid/active L1/L2/Linf均有限且为0，18份速度/涡量Cell_D文件SHA256与修改前完全一致。二维/三维正式程序已更新，src与构建源逐文件一致，未重复接入测试。

Shell语法、Python AST、文档路径与diff检查通过。源、输入、命令、日志、程序、输出、旧程序、指纹与检查保留在runs/temporary/other_coordinates/round1：[检查](../../runs/temporary/other_coordinates/round1/job621982/checks.json)、[源指纹](../../runs/temporary/other_coordinates/round1/source_sha256.json)、[安装](../../runs/temporary/other_coordinates/round1/production_install.json)。用户已有日志删除不纳入提交。
