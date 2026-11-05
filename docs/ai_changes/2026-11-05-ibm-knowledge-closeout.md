# IBM 开发知识收尾（2026-11-05）

范围：本会话的 BOX3D_OSI 三维/二维静止 IBM、此前误选范围的独立 Cylinder3D，
以及相关文档、根规则和实验材料。不是全部 workspace 内容审查或代码结构审查，
除一处过期维度说明注释外，没有修改数值代码、配置、运行脚本、用户后处理/图片/PPT/MLUPS 修改或生成记忆。

## 事实面与状态

| 事实面 | 状态 | 依据与处理 |
|---|---|---|
| 代码 | changed-and-verified | 数值基线 72c77fa，修正一处误称 IBM 三维专属的注释；BOX3D_OSI 两维 ibm.enabled 默认 false，disk/sphere/cylinder 按维度选择；Cylinder3D 为独立实现，默认 true/sphere/idf |
| 运行态 | verified-current | 626182/626183/626190/626200/626201 当前均 SUCCEEDED；根两维程序 SHA256 与 ibm_2d/d2、d3 验收快照相同；未重启生产作业 |
| 文档 | changed-and-verified | 就地修正架构的二维 IBM、粒子初始化、Geometry 和 restart 契约，补验证/脚本索引及跨算例辨别指针 |
| 规则 | verified-current | AGENTS.md → CLAUDE.md 软链有效，无目标算例级 override；上级无附加规则，全局 Codex AGENTS 为空；无须增加规则或复制算例事实 |
| 记忆 | pending / generated-read-only | Codex 生成 MEMORY.md:1022 仍含二维 IBM 不支持的历史快照，当前代码和文档已取代它；没有单独更新记忆的直接授权，未手改生成文件或写 correction input |
| 工作区 | pending | 单一 Amrex worktree；用户既有 dirty 文件保留，本轮只提交文档。缓存仅预览，未执行删除，不能声称整个工作区干净 |
| 远端发布/服务 | not-applicable | 本次为本地仓库与批处理实验收尾，没有 PR、合并、部署或服务切流请求；未宣称 deployed/live verified |

只读盘点完整机械枚举了 695 个 Markdown/MDX 路径（含依赖和测试夹具），
全文阅读限规则、索引、本次 IBM 命中文档与必要源码。快照记录的起始 HEAD 为 72c77fa。
盘点材料见 [inventory.txt](../../projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/ibm_closeout/inventory.txt)。
没有将夹具中的假规则、其他算例历史记录或用户修改当成本轮待修对象。

## 唯一现役入口与修正

- 使用合同以 [BOX3D_OSI README](../../projects/3Dcases/Computing_performance_test/BOX3D_OSI/README.md#二维与三维静止-ibm-开关) 为准；
  [架构](../../projects/3Dcases/Computing_performance_test/BOX3D_OSI/docs/osi_algorithm_and_architecture.md#12-checkpoint输出与-restart)
  负责力场、布局和 checkpoint 生命周期，不再保留“二维未接入”或“输出开关决定施力”的现役说法。
- 新算例在 ibm.enabled 或 checkpoint.write_particles 为 true 时创建标记；IBM 开启会强制保存
  重启必需的标记。Geometry 已保存中心/半径，运动速度等刚体状态仍不完整；不再笼统说全部 host 状态未保存。
- Restart 的 dual-grid 路径会使粒子转为私有 ParGDB，静止耦合需 Define 重绑动态流体布局，
  不能仅清除 AmrCore 侧覆盖项。宏观场半步修正、原始矩施力插值、四点核支撑及关闭路径边界已写回架构。
- [脚本索引](../../projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/README.md#静止-ibm-验证入口)
  指向现有复现入口，说明需要冻结 inputs/app.ex/baseline.ex，必须选择新目录；旧验证现场只读保留。
  没有新增一次性提交入口或将实验脚本提升为生产默认配置。
- [运行索引](../../projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/README.md#静止-ibm-代码验证)
  分开列出成功验收与早期失败，不用目录名、退出码或构建成功冒充数值正确性。
- [Cylinder3D README](../../projects/Cylindertest/Cylinder3D/README.md) 明示其独立性：ibm.geometry/method、
  默认值、checkpoint 与验证范围不同。此前 fd9b413 的代码没有擅自回滚或混入 BOX3D_OSI；
  若要撤销这次误选算例产生的独立改动，需要用户另行裁定，不通过知识收尾隐式删除功能。

## 验证证据和保留边界

- BOX3D_OSI 首次三维验收：[626190 的 22 项报告](../../projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/ibm_switch/accepted/verification.json)。
- 当前二维：[626200 的 21 项报告](../../projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/ibm_2d/d2/verification.json)，
  三维兼容：[626201 的 22 项报告](../../projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/ibm_2d/d3/verification.json)。
  本次复核 JSON、冻结程序哈希、当前根程序与快照一致性及作业状态，没有重复求解。
- 二维各独立对照 valid DDF 为零差；三维耦合/重启/MPI 对照最大差 1.79e-7（FLOAT）。
  范围为 32 个 coarse step、1/2 GPU，主比较两层、三层仅有限值；不外推长期、收敛、性能、多节点、运动刚体或全部 ghost。
- Cylinder3D、ibm_switch、ibm_2d 数据目录约 76 GiB、2.6 GiB、1.5 GiB，均含复现/失败证据。
  这些数据、输入、命令、日志、报告、冻结程序和关键脚本保留，不按失败、较早或 temporary 名称删除。
- 非周期 physical-ghost 的既有 source mismatch 仍存在；边界仍是方腔，圆柱没有端盖，
  不是入口/出口圆柱绕流物理验证。以上 warning 没有被本轮文档收尾消除。

## 清场预览与待决

唯一缓存候选：`projects/Cylindertest/Cylinder3D/runs/temporary/ibm_runtime_switch/baseline_build/tmp_build_dir/`。
212 个可再生构建文件，分配约 121 MiB；多数与现场构建对象硬链接，按当前独有 inode
预计仅能回收约 19 MiB，不能把名义体积当成实际释放量。baseline 源码/config、构建日志、
可执行文件、输入和数值证据均不在候选中。精确字节与保留范围见
[cleanup_preview.json](../../projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/ibm_closeout/cleanup_preview.json)。

未执行清场，也未新增/删除 worktree 或分支。依据本次显式调用的
[neat-freak](../../.agents/skills/neat-freak/SKILL.md)：“只有用户看完汇报后明确确认可以清场，才执行删除”。
复核现场仍保留，等待用户确认后才能处理上述缓存；关键复现材料不随清场自动删除。
生成记忆的旧说法仍待获授权的宿主更新；其他算例、其他生产作业、用户 dirty 文件、
PPT 内容与全局平台配置为 out-of-scope。本次未宣称全仓清洁或所有知识面完全闭合。

## 本轮验证

受影响文档及根规则/README 的 260 个本地路径与显式/标题锚点检查通过，
包含到本收尾记录的新入口。源文件与数值基线逐字节对照，仅一条中文注释不同；
本轮不重复编译或 GPU 求解。根程序/冻结程序的 SHA256 复核、JSON 报告读取、
Git/worktree/规则盘点和空白检查通过。只读检查材料位于
`projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/ibm_closeout/`。
本次只提交知识面和注释修正，不提交用户既有改动；所有删除候选仍留在原处。

## 用户确认后的清场结果（2026-11-05）

本节更新首次汇报中的待决状态；此前预览和授权门记录保留。
用户在完整汇报后明确回复：“误改保留, 生成记忆旧条目能否清理?候选清理删除”。

- Cylinder3D 的 fd9b413 独立改动经用户选择保留，不再作为待裁定回滚项。
- 已仅删除预览中的 baseline_build/tmp_build_dir，212 个文件，预计回收 19,677,184 bytes
  （约 18.8 MiB）。其他实验目录、源码、配置、可执行文件、输入、日志、报告、
  PlotFile 和 checkpoint 未删除。保留的 15 个基线文件和 204 个现场硬链接对象
  均逐文件 SHA256 验证相同；候选目录已确认不存在。
- 生成记忆纠正已获用户明确授权。按环境约定提交
  `extensions/ad_hoc/notes/2026-11-05T143500-ibm-dimension-correction.md`，
  纠正“二维 IBM 仍不支持”的旧说法，并记录 Cylinder3D 保留、两算例参数不同及验证边界。
  该输入内容与准备稿逐字节相同。没有直接编辑生成 MEMORY.md；旧条目当前检索到
  第 1119 行，行号会随生成变化，最终索引清理仍为 pending-host-consolidation。
- 没有重复运行数值计算。清理凭证与逐文件保留清单见审计目录中的
  [cleanup_result.json](../../projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/ibm_closeout/cleanup_result.json)
  和 [cleanup_retained_manifest.json](../../projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/ibm_closeout/cleanup_retained_manifest.json)。
  用户既有 dirty 文件未暂存，未执行其他分支/worktree/目录清理；全仓并非干净状态。
