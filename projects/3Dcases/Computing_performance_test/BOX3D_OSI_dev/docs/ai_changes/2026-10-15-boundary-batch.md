# 批量执行物理边界任务

## 目的、范围与实现

在 BOX3D_OSI 中减少 `ApplyPhysicalBoundaryLevel()` 逐 Fab、逐工作盒启动 GPU
kernel 的开销。基线为 `5145b1e`；保留现有厚工作盒、非平衡外推公式、周期判定、
covered 筛选及平均后修复范围。调度函数为 canonical/OSI 共用，性能测量只针对 OSI。

- 复用 `amrex::TagVector`，由 `BuildBoundaryWorkBoxes()` 在布局构建时生成本层
  rank-local 普通推进和平均后修复两组任务；每次调用批量执行对应任务。
- `boundary_work_cache` 接替原来唯一用于边界执行的 `boundary_work_boxes`，
  不另保留第二套持久盒列表；两组任务来自相同的几何判定，修复组与 valid 求交。
- tag 只保存工作盒、grown-Fab 几何、本地数组索引与相位表索引。
  `MultiArray4` 每次从当前 `state_lev` 取得，因此 canonical 的 `f_old/f_new`
  交换不会让缓存绑定旧存储；OSI 偏移读现有 `osi_fab_phase_cache` 的当前相位。
- 缓存只由几何构建入口更新，在 regrid 前、缓存重建和清层时失效。
  原有 `boundary_full_cells`、`boundary_launch_cells` 统计口径保持一致。
- GPU tag 类型位于同一头文件的 `Box3dDetail`，满足 NVCC 对扩展 device lambda
  中模板参数类型可访问性的要求；没有新增生产配置开关或旧路径分支。

修改限于本算例的头文件、初始化、AMR 生命周期与边界执行，另加入独立回归测试，
同步架构文档。用户原有 `docs/MLUPS记录.md` 修改未纳入提交。

## 数值与构建验证

- 基线及候选均通过 `GEN_CCDB=0 MAKE_J=8 ./scripts/compile.sh --no-submit`
  CUDA+MPI 构建，使用本算例指定的 AMReX 26.06、GCC 11.3、CUDA 12.8、HMPI 1.2。
- 新增 `tests/build_boundary_batch_test.sh`、`BoundaryBatch.GNUmakefile` 与
  `boundary_batch_test.cpp`。测试直接提取生产 tag/cache 类型、构建及执行函数；
  独立参考逐 Fab 扫描整个 grown 区域，按原厚盒条件、mask 与真实边界公式计算。
- [数值作业 613458](../../runs/boundary_batch_20261015/job613458/) 为 `SUCCEEDED`。
  独立 GPU 测试在 1/2 rank 各通过 432 组，覆盖全部八种周期组合、不同尺寸和非均匀
  Fab、无掩码/跳过 covered/平均后修复、canonical 双缓冲交换、布局替换及空 rank；
  phase 为 0、17、最大 uint64。比较全部 27 个分量、完整 grown 存储（含未写单元
  和 ghost），全部有限且 `linf=0`。
- 三种插值模式各 64 步动态 AMR：前后各 1484 条数值/阶段记录及各 7 个 checkpoint
  文件相同；两 rank 动态 AMR 为 1487 条记录及 10 个文件相同。
- 混合周期、全周期各 64 步单层固定网格：各 768 条记录及各 3 个 checkpoint 文件相同。
  每组均通过现有 `check_osi_stage_completeness.py` 的逐步、逐层完整性检查。
  checkpoint 相等覆盖保存的 valid DDF，不把它扩展为全部生产 ghost 等价证明。
- 混合周期动态重网格的历史基线问题不在本次验证范围；混合周期使用固定网格。
  MPI 验证限于单节点 host staging，性能结果限于单 rank/单 GPU。

独立测试构建入口（先加载算例工具链）：

```bash
BOUNDARY_BATCH_BUILD_DIR="$PWD/runs/boundary_batch_20261015/unit_build" \
  MAKE_J=8 ./tests/build_boundary_batch_test.sh
```

运行命令、可执行文件及输入指纹均随作业保存；1/2 rank 日志见上述数值目录。

## 性能结果与限制

[性能作业 613457](../../runs/boundary_batch_20261015/job613457/) 为 `SUCCEEDED`。
`whshare-agent-89`，A100 PCIe 40 GB，单 GPU/1 rank；128³、最多三级动态 AMR、
Re=1000、每 32 步重网格，关闭锁步、输出和提前停止。每种最大 Box 五对交替运行，
从相同的第 96 步基线 checkpoint 重启，推进至第 608 步；排除第 128 步首个统计窗口，
统计第 192–576 步七个 64 步窗口，共 448 步，末尾未报告的 32 步不计入。
百分比取每对 `(基线时间−候选时间)/基线时间` 的中位数，不是两组中位耗时的比值。

| 最大 Box | 边界耗时降低 | perf total 降低 | perf solv 降低 | 边界更快的对数 | total 更快的对数 |
|---|---:|---:|---:|---:|---:|
| 32 | 27.24% | 10.88% | 11.17% | 5/5 | 5/5 |
| 128 | 18.28% | 1.38% | 1.41% | 5/5 | 5/5 |

两种划分均为五对方向一致，保留候选。前后逻辑边界单元计数完全相同；Box=32
另核对五对的碰撞单元数均相同，不存在少计算层级或格点带来的虚假加速。
这里的逻辑单元数不包含 CUDA 尾部填充线程，批量任务的 block/warp 组合会改变。

Box=32 计时波动较大：边界改善范围 21.69%–77.92%，整体改善 2.62%–22.86%；
整体变化还包含其他阶段和运行波动，不能将 10.88% 全部归因于边界 kernel 本身。
Box=128 边界改善 18.07%–18.67%，整体改善 1.32%–1.50%。
这些是本次节点、输入和窗口的结果，不承诺跨节点或长期运行保持同一幅度。

[完整配对数据与验证汇总](../../runs/boundary_batch_20261015/job613457/analysis.json)，
复查命令为仓库根 `.venv/bin/python` 调用实验目录的
`analyze.py job613457 job613458`。基线/候选源码、可执行文件、unit 构建日志、输入、
命令、阶段检查和指纹保存在[实验目录](../../runs/boundary_batch_20261015/)。
