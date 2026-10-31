# 计时与对比检测精简

## 目的与范围

精简 BOX3D_OSI 主循环计时、验证调度及 checkpoint 对比中的重复工作。
范围为 `src/main.cpp`、`AmrCoreLBM.H/.cpp`、`AmrCoreLBM_diagnostics.cpp` 与算例 README；
不改变数值核、输入默认值、推进顺序、AMR 判据及各阶段比较范围。

## 主要改动

- 复用 `amrex::second()`，窗口以 double 秒累计并整体清零；旧日志行仍用毫秒，
  `perf(s)` 仍用秒。不复用析构时同步 GPU 的 `ScopedPerfTimer`，避免改变外层测量行为。
- 删除没有更新者的五个计时成员及输出：`average_copy`、`average_scale`、
  `average_restrict`、`osi_decode`、`osi_encode`。现有绘图解析器已兼容缺少旧细分字段。
- 生命周期调用处交由已有 `OsiAbVerification` 判断开关，底层诊断保护保留。
  `osi_ab_continue_on_mismatch` 复用 `ProbeParams` 的读取与生命周期，由 `ReadParameters`
  初始化一次，阶段比较与步末报告只读该值，不增加并行配置来源。
- checkpoint 坐标分类复用原有逐值算法，将循环改为 Fab → q → cell，
  每个 Fab 的全部分量和两份掩码只回拷一次，避免每个 q 重复回拷全部 Q 分量。
  新增局部 `MismatchStats[Q]` 只保存当前 level 的各分量分类累计量；函数局部构造、
  每层重置、分类循环唯一更新，不形成跨调用缓存。分类前显式等待设备完成，
  代替原先由前置 norm 隐含完成的等待；valid/active 范数的屏蔽与归约顺序保留。
- 保留 CPU 独立逐值复核、GPU 阶段对比、Stream 源检查及 CUDA 事件计时；
  checkpoint 分类仍沿用原有 rank 本地计数与坐标输出语义，不在本次扩大为全局归约。

## 验证与复现

基线为 `07a8b87`，先完整编译并冻结源码、输入和程序，再编译候选程序。
本轮为临时验证，材料保留于 `runs/temporary/timing_verification_cleanup/round1/`：
基线/候选源码与程序、哈希、补丁、编译日志、提交脚本、分析脚本、逐例命令、运行日志与检查点。

- 默认三维 FLOAT、CUDA+MPI 编译通过；已有 `LbmLattice.H` 的 always_inline 属性警告仍在。
- 作业 `621819`：43 组运行正常退出，覆盖 A-B/OSI、单/双 rank、动态细层创建、重启与验证钩子。
  八组生产对照每层 valid/active 范数均为 0，30 个 checkpoint 层级 DDF 文件逐字节一致；
  八组非零误差的范数及坐标分类输出一致；单层及 AMR 锁步阶段失败记录为 0，诊断输出一致。
- 78 个候选计时窗口、重启首窗实际步数、MLUPS 分母及原有绘图解析器检查通过。
- 初轮 400 步计时中位差 +10.507%，窗口尚在变化；2026-10-31 作业 `621975` 补充五对同节点交替复测：
  128³、单 Fab、单 rank、4000 步，固定排除前 1000 步；配对中位差 +0.002711%，
  范围 −0.118386% 到 +0.049899%，未复现短测差距。本条件下未观察到明显性能回退，不宣称通用加速。
- 加长计时组各次整次 Arena 活跃分配高水位均为 365 MB，分配池峰值均为 30254 MB；
  整次 Total Time 和逐对结果见任务目录 `report.md` 与各 job 的 `report.json`。
  allocator 统计不代表整个设备/进程显存峰值。
- 验证只覆盖三维 FLOAT、小网格至一个细层及上述单层计时；不代表二维、DOUBLE、
  更深 AMR、粒子或长期物理精度验收。原始数据、日志、检查点与复现入口均保留。
