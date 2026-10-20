# 合并二维兼容实现

目的：将 BOX3D_OSI_dev 已验证的二维兼容实现合入 BOX3D_OSI，后续以主算例为统一维护入口。

范围：主算例 src、二维输入、构建/提交维度选择、现有验证工具及使用文档。
dev 只增加归档说明；其源码、程序、输入、历史运行、日志和报告均保留。

主要改动：

- 复用 dev 的 LbmLattice.H、D2Q9/D3Q27、维度安全索引、插值/平均、输出、checkpoint 元数据和静态圆盘容器，替代旧 D3Q19.H；没有新增格子数据来源或并行推进路径。
- 原样保留主算例 config/inputs（Re=3200）。三维保留 a415c9b 的 state_error_6 与 n_start=-2；二维沿用 dev 的 state_error_3。受控回归夹具仍默认关闭。
- 合入 DIM=2/3 编译和编译数据库选择；二维提交读取 inputs.2d。保留主算例 guard_run.py、rank 退出记录及超时控制；运行目录冻结 rank 包装器的维度默认值，避免依赖 MPI 自动继承维度环境。
- 复用 dev 现有测试入口；主算例更新较新的脚本契约测试保持原样。历史基线须通过现有环境变量显式指定，不复制或改写历史数据。

验证：

- 二维/三维生产内核 ASan/UBSan 测试、OSI 索引检查通过。
- Shell/Python 语法、现有 OSI 脚本契约和 TinyProfiler 提交契约检查通过；两维 rank 包装器保留退出码与 JSON 退出记录。
- DIM=2/3 CUDA+MPI 构建通过，日志为 `logs/compile/compile-20261020T200908.log`、`logs/compile/merge3d-driver.log`；编译数据库核对为 DIM=3（109 条）。继承的 CUDA always_inline 警告未在本次改动。
- 作业 615942 成功：三维 Re=3200、64 步、max_level=1、2 rank、多 Fab、插值 0/1/2，合并前 OSI 与合并后 A-B/OSI 的 active DDF 逐 cell/q 最大误差均为 0；旧 checkpoint 重启对照为 0。10 个运行均退出 0，报告在 `runs/dimension_merge3d_615942/acceptance.json`。covered coarse valid 的差异不属于 active 结论。
- 三维专项临时脚本从现有 `submit_dimension_validation.sh` 截取三维生产/旧 checkpoint 对照段，避免重放整个初次迁移矩阵；保留 `scripts/temporary/submit_merge3d_validation.sh` 及作业内副本供复现。首次提交 615941 因脚本执行权限失败，未执行求解器；修正权限后为 615942。
- 作业 615945：复用 `submit_dimension_refactor_validation.sh`，以本次冻结的 dev 两维程序为基线。42 个主场景与 12 个旧程序源诊断控制全部通过，阶段逐值与最终 active DDF 最大误差为 0（容差 1e-12）。覆盖两维、插值 0/1/2、1/2 rank、方腔/周期、多 Fab、受控 AMR 的建层/Remake/清层；阶段 64 步、独立生产对照 192 步，二维最高层编号 2、三维 1。报告为 `runs/dimension_refactor_615945/acceptance.json`；该作业使用 fixture，真实三维加密判据另由 615942 检查。
- 方腔 physical ghost 源诊断仍有历史差异，与冻结 dev 控制一致；valid/internal ghost/interface/covered 源没有新增差异。上述“误差为零”不扩展到全部 ghost 存储。
- 基线来源与 SHA256 在 `runs/dimension_merge_20261020/`，作业目录保存源码、输入、冻结程序、命令与状态表；运行数据未纳入提交且未删除。
- 本地文档链接、CRLF 感知 diff 检查通过。

已有 dev 验证详见[历史记录](../../../BOX3D_OSI_dev/docs/ai_changes/2026-10-19-dimension-accessors.md)。
本次不重新证明长期收敛、性能或所有 physical ghost 等价。
