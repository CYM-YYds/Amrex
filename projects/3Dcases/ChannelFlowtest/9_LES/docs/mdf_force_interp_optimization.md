# MDF force_interp_extrap 模板优化日志

Date: 2026-05-27

## 优化要点
1.先把 force_interp_extrap 改成预计算 base index 和一维权重，并把循环缩到非零支撑的 3x3x3。这是最直接、收益最大的点。
2.再处理 atomic contention：当前多个壁面拉格朗日点会频繁写同一批 Euler cell。更大的优化是 tile-local buffer、Euler-cell-centric gather，或按 cell/bin 做局部归约后再写 Ft。
3.然后优化 InterpForce 的 reduce：合并 6 个 reduction，并根据静止/运动颗粒路径决定是否每次都需要统计 F_tot。
4.如果以后把 NF > 1，两阶段分支里 force_delta_lev 每次动态创建 (line 1767) 和每轮 setVal/SumBoundary/Add/FillBoundary 也会变成明显开销；但当前 NF=1，这个分支不是现行瓶颈。

## 范围

此笔记记录了针对 `projects/3Dcases/ChannelFlowtest/9_LES` 情况下
`src/Kernels.H` 中 `force_interp_extrap()` 的首次优化迭代。

当时的普通 MDF 活动运行路径为：

1. `main.cpp` 调用 `JaberCycle()`。
2. `JaberCycle()` 在最细网格上调用 `AmrCoreLBM::ComputeParticle()`。
3. `ComputeParticle()` 调用 `AmrCoreLBM::InterpForce()`。
4. 在 `NF = 1` 时，`InterpForce()` 使用 `LagrangeParticleContainer::InterpForce()` 的单次 MDF 重载。
5. 这会对每个本地拉格朗日点调用一次 `force_interp_extrap()`。

截至 2026-06-15，`9_LES` 的默认 `NF=1` 路径已切换到壁面模型：

```text
AmrCoreLBM::InterpForce()
  -> LagrangeParticleContainer::InterpForceWallModel()
  -> Kernels.H::force_wall_model()
```

因此本文记录的 `force_interp_extrap()` 优化仍可作为普通 MDF 内核参考，但不再是
当前 `9_LES` 默认粒子力路径的性能瓶颈。壁面模型实现与验证见
`docs/壁面模型AI实现手册.md` 和 `scripts/check_wall_model_nf1.sh`。

## 问题

旧内核对每个拉格朗日点执行了两次 `5 x 5 x 5` 模板扫描：

- 一次用于从欧拉格点插值到拉格朗日速度和密度
- 一次用于从拉格朗日向欧拉格点扩撒力

3 点正则化 delta 函数 `delta3p()` 在每个方向上仅对三个格点有非零支撑。在普通内部单元中，旧的 `5 x 5 x 5` 扫描因此评估了许多权重为零的条目，并在两次扫描中重复计算相同的 `floor()` 和 `delta3p()` 值。

## 更改

优化后的内核现在：

- 预先计算 `wx[3]`、`wy[3]` 和 `wz[3]`
- 在嵌套循环中重用 `wxy = wx[x + 1] * wy[y + 1]`
- 对插值和扩撒仅扫描 `x/y/z = -1..1` 的 `3 x 3 x 3` 点

对于当前启用的单次 MDF 分支，在评估预计算权重之前保留原先的 `NX/NY/NZ` 索引环绕行为。两阶段分支保持之前的无环绕行为。

## 预期效果

针对每个拉格朗日点，这将把每次插值或扩撒的模板位置从 125 个减少到 27 个。因此，单次 MDF 内核对每个拉格朗日点的模板访问次数从 250 次变为 54 次，同时减少重复的 `floor()` 和 `delta3p()` 调用。

这并不能消除扩撒阶段的原子竞争；那是另外的优化目标。

## 验证

添加了 `scripts/check_mdf_stencil_optimization.sh` 作为一个窄范围的结构检查。该脚本在旧代码上先失败，而在此更改后通过。

完整编译验证请在该 case 目录下运行：

```bash
./scripts/compile.sh
```
