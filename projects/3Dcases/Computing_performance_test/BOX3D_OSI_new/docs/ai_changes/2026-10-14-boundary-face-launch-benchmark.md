# 边界薄面启动试验：未保留候选实现

## 目的与范围

检查 `BuildBoundaryWorkBoxes()` 将边界启动范围从 `nghost+1` 层厚盒缩到真实
物理面，能否改善 BOX3D_OSI 的边界与整体推进性能。基线提交为
`1414d40e6f37a5f52572ebd6c3c41e642241db82`。本次只试改该函数；它为
canonical/OSI 两种布局共用，不改变边界公式、covered 筛选、推进或通信。

## 候选改动与最终状态

原厚盒与所有非周期物理面求交，再复用 `amrex::removeOverlap` 去重。
不能只缩对应面的法向端点：原厚盒可能还包含另一个物理面上的切向 Fab ghost，
这些写入必须保留。域外 ghost 仍由原来的 `grow(valid_box, nghost) & domain` 排除。
域内邻点仍由边界线程直接读取，无需为邻点启动线程。

候选通过逐值回归，但性能无一致收益，因此已恢复基线源码及可执行文件。
本提交仅记录试验。用户原有 `docs/MLUPS记录.md` 修改未纳入提交。
候选、基线、测试源码、输入、命令、指纹与日志均保留在
[实验目录](../../runs/boundary_faces_20261014/)，
[候选补丁](../../runs/boundary_faces_20261014/candidate.patch) 可供后续复查。

## 验证

- 前后版本均通过 `GEN_CCDB=0 MAKE_J=8 ./scripts/compile.sh --no-submit`
  CUDA+MPI 编译，AMReX 26.06、GCC 11.3、CUDA 12.8、HMPI 1.2。
- [独立检查](../../runs/boundary_faces_20261014/test.log)：直接提取前后生产函数，
  检查 8 种周期组合、ghost 宽度 0/1/2、多个 Fab 及跨面切向 ghost。
  24 组配置的新启动集合恰好等于旧启动集合中落在非周期物理面上的点，且无重复。
- 使用真实 `Kernels.H` 边界公式，CPU 逐值比较 canonical/OSI、phase
  0/1/17/41/最大 uint64，以及无掩码、跳过 covered、平均后 covered-valid 修复。
  比较整个 ring 的 99,532,800 个 raw 存储值（包含未写单元），全部有限且 `linf=0`。
- GPU job 613435 完成三种插值模式各 64 步最多三级动态 AMR 前后对照。
  每组 1484 条数值/阶段记录与 7 个终态 checkpoint 文件完全相同；现有
  [阶段检查](../../runs/boundary_faces_20261014/stage_check_dynamic.log)
  每组确认 1116 条完整阶段记录。checkpoint 相等覆盖保存的 valid DDF，
  不据此声称全部生产 ghost 等价。
- job 613437 完成混合周期及全周期各 64 步单层固定网格前后对照，
  每组 768 条数值/阶段记录及 3 个 checkpoint 文件完全相同；
  [阶段检查](../../runs/boundary_faces_20261014/stage_check_periodic.log) 通过。
- 额外观察：job 613435 的混合周期 **旧版基线**在第 64 步动态重网格时
  `Regrid produced nonfinite valid OSI DDF` 断言失败；保留
  [失败日志](../../runs/boundary_faces_20261014/job613435/mixed_baseline/run.log)，
  终止该作业并改用固定网格检查混合周期边界。本次没有处理这个基线问题。

job 613437 的所有程序均正常 finalize，且脚本输出了完成标记；汇总检查也通过。
调度器在程序结束后仍显示 RUNNING，因此主动终止该测试作业以释放 GPU。
以上通过结论来自程序日志与逐值检查，不将调度状态描述为 SUCCEEDED。

## 性能结果

job 613437，`whshare-agent-187`，单张 A100 PCIe 40 GB、1 MPI rank。
128³、三级非周期动态 AMR、Re=1000、重网格间隔 32；最大 Box 分别为 32/128。
关闭锁步诊断、plot、测试段 checkpoint 和提前收敛停止。
每种划分五对交替运行，均从基线第 96 步的同一 checkpoint 重启，推进 512 步。
丢弃第 128 步首窗口，统计第 192–576 步七个 64 步窗口，共 448 步；
末尾未报告的 32 步不计入。百分比取每对前后比值的中位数，耗时变化正号表示更慢。

| 最大 Box | 启动单元减少 | 边界耗时变化 | perf total 变化 | 边界更快的对数 | total 更快的对数 |
|---|---:|---:|---:|---:|---:|
| 32 | 66.19% | +12.67% | +0.60% | 0/5 | 0/5 |
| 128 | 66.15% | +2.11% | -0.54% | 2/5 | 3/5 |

Box=32 的五对边界、整体计时均变慢。Box=128 的整体变化范围为
-7.15% 到 +1.36%，配对中位数的微小改善与明显波动并存，边界也只有 2/5 更快，
不足以证明稳定加速。最终回退候选，不把线程数减少当作性能收益。

几何上，完整 128³ 域的旧/新盒数在 Box=32 时为 96/98，Box=128 时为 6/8；
薄面裁剪还会改变 kernel 的工作量与线程分布。本次没有 GPU trace，
不将某一因素认定为变慢的唯一原因。

[完整配对数据与验证汇总](../../runs/boundary_faces_20261014/job613437/analysis.json)
包含每对结果；`analyze.py job613437` 可复查汇总（仅使用仓库根 `.venv/bin/python`）。
