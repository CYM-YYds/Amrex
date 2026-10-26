# Restriction 标签去重实验（GPU 结果待运行）

目的：比较原完整 block 标签与“区域属性一份、block 保存区域索引和起始单元”的空间与时间代价。
范围仅为 BOX3D_OSI 的独立冻结快照，不改生产实现和线程映射，不包含用户未提交修改的提交。

## 已完成与实测

- 基线取 `d470c06` 加创建快照时用户工作区修改，完整差异保存在实验目录 `worktree.diff`。
- 基线、候选均通过 CUDA/MPI 编译，AMReX 26.06、DIM=3、CUDA sm80、现有 Q27 半 warp 映射不变。
- 同一编译工具链的独立 `sizeof` 实测：原 block 标签 80 B；候选 block 标签 16 B，区域标签 64 B。
- 设区域有 B 个 block：原占用 80B 字节；候选 16B+64 字节；节省 64(B−1) 字节。
  这是逻辑载荷，不等于整个程序显存节省比例；多 block 区域的标签节省比例趋近 80%。
- `bash -n` 与 Python 语法解析通过。尚无 GPU 数值/性能结果，不据此推荐合入生产。

## GPU 对照设计与状态

同一节点资源分配内 baseline/split 按 ABBAABBA 顺序执行，每种四次；1、2 rank 分别测试。
输入保持相同：Re=3200、64³ 基网格、level 0/1/2、max_grid_size=32、BF16、regrid_int=32、interp_mode=0，host staging。
从相同初态运行至 256 步，排除前 64 步；统计 65–256 步三个窗口。
主指标为 `perf(s).average` 与 `total`，多 rank 每窗口取较慢 rank，不相加。
这些指标不能单独隔离新增区域表访问的 kernel 耗时。重构时额外输出逐层逐 rank 标签字节及构建上传时间。

数值检查单独运行至 64 步：三层 valid DDF 对比基线 checkpoint，并逐字节比较首次界面平均前后快照及 mask。
性能运行不启用这些导出。分析脚本检查每层推进和平均工作量一致，再生成 `analysis.json`、`layout.json`、`workload.json`、`report.md`。

- 双 GPU 作业：619980，最后查询 PENDING，没有满足要求的空闲 GPU。
- 单 GPU 作业：619982，最后查询 PENDING。
- 619981 因新提交脚本缺少可执行权限退出，未运行求解器；已修正权限，不纳入统计。

## 保留与复现

实验材料：`runs/temporary/restriction_tag_layout/`，标为临时实验，但保留输入、源快照、差异、可执行文件、日志和数值证据。
内有 baseline/split 独立构建、`candidate.patch`、`source_sha256.json`、编译日志及 `*_sizes.txt`。
作业结果保存在其 `job<id>/` 子目录；GPU 测试尚未结束，构建中间对象先保留，避免破坏运行依赖。

入口：`scripts/temporary/restriction_tag_layout/`。已有通用算例入口不能生成两种独立标签结构及对照序列，因此此处使用本任务临时驱动，不新增长期入口。
`prepare.py` 仅用于从工作区创建全新实验快照，已有实验目录时拒绝覆盖；`sizes.py` 复用快照编译参数测量尺寸。
`submit.sh` 默认双卡测试，可用调度器 `-R 'cpu=8;mem=49152;gpu=1' -x TAG_RANKS=1` 执行单卡；脚本使用已有 MPI 启动方式。
若作业提交时调度器冻结了旧脚本、未执行末尾分析，可手工使用仓库 `.venv/bin/python` 运行 `analyze.py <job目录>`。

结论边界：仅确认结构尺寸和可构建，GPU 资源阻塞测量；实际总节省、数值等价及时间差仍待结果，生产代码未采用该方案。
