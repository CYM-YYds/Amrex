# 涡量判据、BF 对照与续跑的知识收尾

- 目的/范围：按 neat-freak 收尾本轮 BOX3D_OSI 工作，修正文档中的旧默认值、运行中状态和覆盖/显存结论边界。采用完整知识核对路径，不进行发布、结构重构、全仓库清场或新增数值实验。
- 盘点：运行 skill 的 audit-inventory.sh，机械枚举本算例 Markdown（收尾前 94 个），按索引阅读受影响文档。初始 HEAD=ef9f498、工作区干净、detached HEAD、单一 worktree；用户已自行提交输入空白修改及部分旧监督凭证清理，不恢复或重写其提交。

| 事实面 | 状态 | 结论与证据 |
|---|---|---|
| 代码/配置 | verified-current | 运行时 BF16、最高 level 2、Re=3200；三维 state_error_6 映射仍取编译期 3，默认诊断关闭。本次只修 inputs 注释，数值键值未变、求解器源码未变。 |
| 运行态 | verified-current | 616234/616235 正常结束；其他 OOM、I/O 失败与手动终止分开登记。调度查询无运行作业，本轮监督/采样进程未发现存活。 |
| 文档 | changed-and-verified | README 对齐现役配置；架构集中解释 tag、BF、grid_eff 和 E/F；current_status 汇总结果，历史记录补终态，MLUPS 页明确不把覆盖诊断当性能基准。 |
| 规则 | verified-current | 根 AGENTS.md→CLAUDE.md 软链有效；作用域内无更近规则覆盖，Codex 全局 AGENTS.md 为空。规则无需修改。 |
| 记忆 | verified-current / generated-read-only | 只读检查生成索引用于寻找既有生命周期约定；未获得直接更新记忆的请求，不写生成文件或 correction note。稳定机制已放项目架构文档。 |
| 工作区 | pending（清场预览） | 文档修改已验证；两个本轮临时入口列为候选，复核现场保留。本次未删除脚本、日志、数据、软链、分支或 worktree。 |

## 本轮事实纠偏

- 默认三维 Re=3200、BF16；lbm.err 的数值由二维 state_error_3 使用，三维 state_error_6 不用它们，但 ErrorEst 仍保留数组长度检查。未擅自将编译期层号 3 改成实际层号 2。
- BF 对照的终点指标与 400 行证据，以 [测量记录](2026-10-21-bf32-bf16-tag-coverage-measurement.md) 和其运行目录报告为准。已复核两个作业退出 0、8 个 rank 返回 0、每方案 200 组阈值/打标自洽；原始日志及覆盖 CSV 仍在。
- job615925 为重网格显存耗尽；job615927 是用户手动 terminate，调度 FAILED 不等于 OOM；job615992 在首次 PlotFile 写入报 VisMF::Write failed，只有残缺输出、没有新 checkpoint。原计划续跑 32000 步未完成，不能从正常短测退出推导收敛。
- 本轮六个带显存采样的运行目录，其 gpu-memory.csv 均为指向已缺失 logs/submit 文件的软链。峰值 JSON 和采样工具副本仍在，但历史原始样本无法重算；不恢复或伪造 CSV。相关死链接文案已改为证据缺口，覆盖计数结论不受影响。
- 当前 AMR 的 OOM 是版本、布局和重网格峰值特定结果，不是“单卡均匀 512³ 普遍不可行”的证明；MLUPS、实际更新总量和达到相同精度的成本必须分开。

## 清场预览与未决项

候选为 `scripts/temporary/submit_restart_memory.sh` 和 `summarize_restart_memory.py`：
均有 AI 临时标记，相关作业/进程已结束，未发现正式脚本/测试入口依赖；各自至少六份逐字相同的运行目录副本仍在，可保留复现。此次保留工作区入口供最终汇报后复核，未执行删除。
同目录 submit_merge3d_validation.sh 属于此前维度迁移任务，不纳入本轮清理；全部运行目录、源 checkpoint、输出和报告保留。

pending：VisMF 写入失败根因；缺失原始显存 CSV 的恢复来源；编译期/运行时最高层号的语义统一及其数值验证；相同精度下的 BF 成本比较；上述两个临时入口的后续清场。
out-of-scope：其他算例、整个 workspace、结构审查、已有 physical-ghost 数值问题的重审、二维合并验证的扩充、远端推送/发布、记忆写入。
not-applicable：生产部署与页面/API 上线验收。
warning：当前 checkout 为 detached HEAD；615927 缺本地 exit_status.json，终态以调度 terminate 事件为证；残缺 PlotFile 不可用于后处理结论。既有历史验证范围不因本次文档同步扩大。

## 验证

新旧 inputs 的非注释键值相同；本次无求解器实现变化，不额外构建或重复 CFD。检查所有受影响 Markdown 的本地路径/锚点及根规则引用，缺失采样链接已明确撤下，其他链接与新增 closeout 入口验证通过。CRLF 兼容 git diff --check、现有临时 Shell/Python 语法、规则软链和工作区差异检查通过。只提交本轮文档/注释，不清理复核现场。
