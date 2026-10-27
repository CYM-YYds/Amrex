# BOX3D_OSI 单/双精度第一版

日期：2026-10-27。

## 目的与范围

为官方 BOX3D_OSI 增加默认双精度、显式选择单精度的实验入口，复用 AMReX 26.06 的
`PRECISION` / `amrex::Real` 机制，不复制求解器。改动涉及该算例、其 README、生产头文件
测试，以及直接依赖的公共编译脚本和根 README。公共入口仅在显式指定
`AMREX_BUILD_PRECISION` 时传递 make 参数，未指定时保持其他算例原行为。

## 主要改动

- 构建、远程参数转发与提交支持 `DOUBLE/FLOAT`；默认 DOUBLE，拒绝其他值。
- 数值核和格子定义的浮点字面量使用 `_rt`，数学函数显式使用标准重载，避免单精度字段
  与默认 double 常量或全局数学函数混用。没有改动生产收敛容差、AMR 判据或诊断阈值。
- 粒子存储保持默认 `ParticleReal=double`，修正与场内核之间引用参数的真实类型；
  处理粒子中心初始化的窄化转换。未声称完成运动粒子/IBM 的单精度数值验收。
- 性能统计保留 double，总耗时的起点和累计也明确使用 double，避免切换场精度后损失计时精度。
- 编译数据库在维度或精度切换时重新采集，避免混合记录；运行清单写 precision，
  应用打印实际 Real 字节数。没有增加求解器运行状态或派生缓存。
- 扩展已有生产头文件测试到 2D/3D × DOUBLE/FLOAT，测试覆盖矩、碰撞、插值、平均、
  OSI 寻址、宏观量、涡量和边界；FP64 保持原绝对容差，FP32 用机器 epsilon 定义舍入容差。

## 验证

- 三维 DOUBLE/FLOAT 均通过 CUDA 12.8、GCC 11.3、HMPI 构建（SM80）；使用正式编译入口，
  确认远程转发、精度切换时数据库重新采集。最后留下的数据库为三维 DOUBLE、9 个算例源文件，
  无 FLOAT 记录。完整构建日志在实验根目录 `build_float_final.log`、`build_double_final2.log`。
- 二维/三维 × DOUBLE/FLOAT 的生产头文件测试全部通过。命令：
  `ASAN_OPTIONS=detect_leaks=0 bash tests/run_dimension_kernels_test.sh`。
  地址与未定义行为检查保留；LeakSanitizer 因当前环境不支持而关闭，未声称通过泄漏检查。
- Shell 语法、无效精度拒绝、公共 make 参数显式/未设置行为、产物提示精度筛选、现有 OSI
  脚本模拟回归均通过；Markdown 本地链接检查通过。CRLF 源码使用
  `git -c core.whitespace=cr-at-eol diff --check` 检查，保持原换行格式。
- GPU 作业 `620466`：15 组均 exit=0。64 步，3D D3Q27，Re=3200，基础网格 32³，
  level 0/1，max_grid_size=16，BF=8，固定 regrid fixture，三种插值模式，
  双 rank、host-staging；最终状态检查通过。每种插值分别运行冻结基线、当前 DOUBLE、
  当前 FLOAT；另用 DOUBLE 程序以单 rank 读取 FLOAT checkpoint 并比较 DOUBLE checkpoint。
  跨精度读取只用于终态比较，未在转换后继续推进。
- 三种插值的 DOUBLE 相对冻结基线 active DDF 均 `linf=l2=0`。
  FLOAT 固定周期 level-0、多 Fab、双 rank 的阶段对照共 1560 条，全部 `linf=0`，
  保持原严格容差。该结论不扩展到动态多层 physical ghost 的逐值等价。

| 插值模式 | FLOAT 对 DOUBLE 的 active DDF Linf | active DDF 相对 L2 |
|---|---:|---:|
| 0 | 9.686403518e-7 | 8.336952506e-7 |
| 1 | 9.576374406e-7 | 8.636187043e-7 |
| 2 | 1.079088334e-6 | 8.526400238e-7 |

以上是短测观测值，没有把它们设为生产误差容差。逐值比较使用相同 BoxArray，
active 范数排除被细层覆盖的粗层点，跨层直接累加而不作单元体积加权；
不把 covered 存储或 physical ghost 包含在全局结论中。
输入、程序、命令、终态 checkpoint、`status.tsv`、`summary.json` 位于实验根目录
`job620466/`。汇总使用 `scripts/temporary/precision_switch/summarize.py <job目录>`。
初次作业 `620461`、`620465` 因临时驱动缺少执行权限而未启动应用，修正后才提交 `620466`；
这些失败日志保留，未计入数值验证。

## 边界与复现

第一版为全局场精度切换，粒子精度仍独立。host/几何和粒子表达式可能仍包含双精度运算，
不声称生成的所有 GPU 指令都是 FP32；`--use_fast_math` 沿用已有设置。
没有进行性能 A/B，也未验收 Re=3200 长程稳定性、生产精度、动态 tagging 的跨精度一致性、
二维完整 CUDA 推进或运动粒子重启。

临时实验根目录：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/precision_switch/`。
其中 baseline 冻结了任务开始时含用户未提交修改的源码，不能等同于 HEAD；原有
`AmrCoreLBM_advance.cpp` 和 `Kernels.H` 的用户修改不属于本次提交。
临时验证驱动在算例 `scripts/temporary/precision_switch/validate.sh`，保留用于复现，
输入、源码、程序 SHA256、命令、日志和 checkpoint 均在上述实验目录保存。
