# 插值与界面平均缓存的所有权整理

## 目的与范围

仅整理 BOX3D_OSI 的粗细层传输缓存，减少平行数组与分散的生命周期维护。
不改变插值/平均核、尺度转换公式、phase 映射、计算区域或任务执行顺序。

## 主要改动

- 按细层组织 `InterpolationLevelCache`，拥有 coarse staging、patch 描述、通信计划和两组设备任务。
- 按粗层组织 `AverageDownLevelCache`，拥有稀疏 interface 结果、fine Fab 映射、通信计划和 block tags。
- `InterpolationPatchTag` 合并精确 fine 工作区与全局 fine Fab 索引；正常推进、初始化、重网格临时插值及诊断共用该描述。
- 删除物理边界派生标志，在已有 staging Fab 遍历中用 Box、domain、周期性直接判定。
- 删除独立 `interp_direct_cache_ready`。OSI 非空任务入口检查实际 stage、通信计划、设备任务定义状态及本地任务数量；允许无本地任务的 rank 参与通信。空的全局任务集合直接返回，不再区分未构建和已构建空集合。
- 失效时先释放所有依赖两端布局/地址的任务，再清理所属数据。保留 A-B、非 CUDA 和诊断所需的全局 fine Fab 对应关系；设备本地索引仍由它生成。

## 验证

- 修改前基线：`6904ecc`，工作区干净；原 2D/3D CUDA 可执行文件经构建入口确认均为最新，并保存在 `runs/transfer_cache_20261022/baseline/`。
- 修改后 DIM=2、DIM=3 的 CUDA+MPI 构建均通过。构建日志保存在 `runs/transfer_cache_20261022/build2d.log` 和 `build3d-final.log`。
- 复用 `tests/submit_dimension_refactor_validation.sh`，通过 `DIM_REFACTOR_BASELINE` 指定上述基线；作业 **617145**，节点 `whshare-agent-66`，状态 SUCCEEDED。
- 54 次运行全部退出 0，现有检查器确认 42 项检查和 12 组基线控制完整且通过。覆盖插值模式 0/1/2、1/2 MPI rank、周期/六面非周期边界、OSI/A-B，以及新建、重建、清层和再次建立的动态 regrid 生命周期。
- 64 步阶段对照：2D 请求最高层 2，13,446 条阶段逐值记录，最大 Linf=0；3D 请求最高层 1，13,050 条记录，最大 Linf=0。检查器确认对应最高层实际建立。
- 192 步修改前 checkpoint 对照：两个维度、三个插值模式及两种存储路径的最终 active DDF 最大 Linf=0。该 fixture 最终存活层为 0/1；不据此声称最终 level 2 对照通过。
- 非周期物理 ghost 来源诊断与修改前的 12 组控制一致；2D 最大诊断 Linf=0.02019539653，3D 为 0.01352278042。非物理来源分类无 mismatch。这里确认的是重构前后诊断不变，不能解释为物理 ghost 已逐值等价；OSI 与 canonical 的 covered valid 区域也不由 active DDF 比较证明等价。
- 可复查结果：[acceptance.json](../../runs/dimension_refactor_617145/acceptance.json)、[运行状态](../../runs/dimension_refactor_617145/status.tsv)。该目录还保留程序、源码/输入快照、差异、SHA256 和逐项运行日志。
- 原有 CRLF 头文件保持行尾格式；`git -c core.whitespace=cr-at-eol diff --check` 通过。源码中旧缓存成员引用已清除。

本次验证不包括性能收益、非 CUDA 后端、3D 最高层 2 的数值验收或长期物理解精度；本次属于缓存所有权整理，不宣称计算加速。
