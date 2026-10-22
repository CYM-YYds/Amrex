# OSI 通信优化知识收尾

## 目的和范围

按 neat-freak 核对 `a9f6076` 通信隐藏与 `6047cb8` 缓冲布局合入后的现役说明。
本次只改本算例文档，不修改求解器、配置、规则、生成记忆或实验数据，不重跑数值/性能实验。

## 文档处置

- README修正三维计时默认值、重叠开关、实际调用链及指标口径；二维与代码缺省值单独说明。
  已删除dev目录的两条死链接改成Git历史读取方法，不将旧目录继续作为入口。
- 架构第6节作为重叠机制唯一解释，明确发送源分类的来源/失效、MFIter同步、stream顺序和phase边界；
  补齐MPI缓冲布局、较新2/4 ranks/AMR验证范围，修正GPU-aware支持未经确认的表述。
- 当前状态以通信重叠和最新验证为首项；早期平均/维度作业保留历史语境，不再冒充当前程序指纹。
- 通信计划区分已采纳、已测未采纳与未实现候选；不再将CPC整块重叠或接收调度泛称未测试。

## 六个事实面

| 事实面 | 状态 | 核验和边界 |
|---|---|---|
| 代码 | verified-current | 开关、FB分类、碰撞子集和stream依赖与源码/inputs一致；数值变更仍为a9f6076 |
| 运行态 | verified-current | 正式2D/3D程序SHA256与实际回归副本一致；本轮审计和六个SUCCEEDED凭证保留，无新部署声明 |
| 文档 | changed-and-verified | README、架构、现役状态、通信计划和本记录检查链接、锚点及事实对应 |
| 规则 | verified-current | 根AGENTS.md仍软链到CLAUDE.md；本算例无额外规则，命中规则引用有效，规则文件未变 |
| 记忆 | out-of-scope | generated-read-only；只作历史检索线索，未获得明确写入请求，不修改或新增记忆 |
| 工作区 | verified-current | 单一worktree/Amrex分支；保留三份用户源码修改，文档单独提交，无本轮清场删除候选 |

数值/性能证据以[实现记录](2026-10-22-osi-collision-communication-overlap.md)和
[实验审计](../../runs/osi_collision_overlap_20261022/audit.json)为准，
程序指纹见[最终核对](../../runs/osi_collision_overlap_20261022/final_artifact_check.json)。
本次仅核对现有证据和当前文件，不扩大原测试范围。
核对136个本地链接/锚点、Git历史读取示例及规则软链均通过；
完成算例文件机械盘点，`git diff --check`通过，无需重建或重跑求解器。

## 保留项与未闭合项

38,711条阶段记录与三层checkpoint逐值一致不等于所有物理ghost等价或长期精度证明。
既有物理ghost来源差异、长期/多节点/CUDA-aware MPI/二维性能限制继续保留；
job615992的历史I/O原因仍pending，未借本次文档收尾改成已修复。

`AmrCoreLBM_amr.cpp`、`AmrCoreLBM_osi.cpp`、`OsiIndex.H`的用户修改不提交、不回退。
实验快照、构建对象、日志、checkpoint和复现脚本保留；此前AI临时入口已完成归档，
scripts/temporary中的其他任务脚本不在本次范围。不删除分支、worktree或数据。
