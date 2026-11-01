# 重构全 valid 检查归入 OSI/A-B 验证

## 目的与范围

生产重构不应因独立诊断参数而分配完整 canonical DDF 并执行有限值归约。
范围限于 BOX3D_OSI 的重构诊断、现有验证调度和 A5 实验入口；不改变数值核、
平均下传、加密判据、网格迁移、缓存重建或输出用的 canonical 转换。

## 主要改动

- 删除 `RefineMesh()` 内的全 valid 解码及 NaN/Inf 扫描，移除独立运行时开关
  `verification.regrid_valid_check`。历史参数若残留，不再触发该检查。
- 复用 `CheckOsiReferenceAllValid()`，在已有 A-B 有限值检查之外显式检查 OSI valid
  DDF；由 `OsiAbVerification` 的既有目标步调度调用。检查覆盖 covered/interface
  valid 单元，仍不读取 ghost。解码临时缓冲在分配逐值比较缓冲前释放。
- 生产模式不执行该检查；锁步模式仅在已选择的目标步执行，首次 FillGhost 检查继续
  由原显式配置控制。不新增状态、helper 或替代诊断入口。
- A5 临时实验入口不再强制开启旧独立参数；终点 `final_state_check` 保持原行为。
  冻结运行目录和历史分析脚本保留，不回写既有实验材料。

## 验证

复现入口：`scripts/temporary/regrid_verification_boundary.sh`。
材料集中保留于 `runs/temporary/regrid_verification_boundary/round1/`，包括修改前后
源码、程序、输入、补丁、编译日志、运行命令、检查点和报告。

- 三维 FLOAT、CUDA+MPI 编译通过；Shell 与内嵌 Python 语法、文档路径和
  `git diff --check` 通过。
- 最终作业 `622316`：9 组运行及汇总断言通过，覆盖单/双 rank、六面非周期/全周期、
  细层创建、布局重构、清除和再次创建。三个生产对照的逐层 valid/active DDF
  差异范数均为 0；六个步点共 44 个检查点 DDF 数据文件与基线逐字节一致。
- 开启锁步时，32 步间隔及指定第 64 步两种调度均按预期执行；没有目标步时不执行
  全 valid 检查。生产模式即使残留旧独立参数，也不触发该检查。
- 初轮 `622314` 的 9 组应用均正常退出，但汇总断言误将终点诊断完整平均后的状态
  与平均前的检查点比较，故未记为整套验收通过。其 active 差异为 0、检查点 DDF
  文件一致；最终回归关闭终点刷新，完成同阶段比较。首轮证据及说明保留。

最终报告：`runs/temporary/regrid_verification_boundary/round1/job622316/report.md`。
本次不测量性能，不宣称已量化消除诊断后的加速；不代表二维、DOUBLE、
device-direct MPI 或长期物理精度验收。
