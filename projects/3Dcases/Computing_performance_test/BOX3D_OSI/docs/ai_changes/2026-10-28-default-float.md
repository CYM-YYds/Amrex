# 默认使用单精度场数据

日期：2026-10-28。

目的：按用户决定，将 BOX3D_OSI 的日常编译和提交默认值设为单精度。

范围与改动：

- `config/GNUmakefile` 的 `PRECISION ?= FLOAT` 适用于直接 make。
- `scripts/compile.sh` 和 `scripts/submit.sh` 的缺省精度同时改为 FLOAT，避免默认编译与默认提交选择不同程序。
- 显式 `PRECISION=DOUBLE` 的编译和提交仍可用，README 同步两种用法。
- 复用已有精度选择和程序命名机制，没有增加状态或新入口；统计 double、粒子精度、数值核及物理输入均未修改。
- AMReX 自身与其他算例的默认值保持原设置；历史实验记录保持当时的精度描述。

验证：Shell 语法检查通过；执行提交脚本的程序选择段（不提交作业），确认未设置精度时选择 `.FLOAT` 程序，显式 DOUBLE 时选择原双精度程序。
未设置 PRECISION 和 AMREX_BUILD_PRECISION 的三维 CUDA+MPI 构建通过（exit=0），生成 `main3d.gnu.FLOAT.TPROF.MPI.CUDA.ex`；编译数据库完整覆盖 9 个算例源文件，全部含 `-DBL_USE_FLOAT`。README 本地链接与 diff 检查通过。此次仅修改默认选择，未重复先前的数值与性能实验。

复现：算例目录下执行 `env -u PRECISION -u AMREX_BUILD_PRECISION ./scripts/compile.sh`。
构建记录保留在 `runs/temporary/default_float/`；构建使用当前工作区，包括用户已有源码修改，本次仅提交上面的默认值和文档变更。
