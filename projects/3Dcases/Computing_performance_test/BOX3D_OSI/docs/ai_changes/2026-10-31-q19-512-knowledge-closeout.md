# Q19与512³测试知识收尾

日期：2026-10-31。范围仅BOX3D_OSI近期Q19与512³实验；不改求解器，不重复计算。

## 事实面

| 事实面 | 状态 | 核对与处置 |
|---|---|---|
| 代码 | verified-current | src逐文件SHA256与other_coordinates/round1/source_sha256.json一致；正式三维Q27程序与两轮冻结Q27 SHA均为fdda4f24…c14。Q19仅实验选择，师兄原件未改。 |
| 运行态 | verified-current | 621996及622016均SUCCEEDED；退出码、19分量回归、末态及性能窗口对应原始日志。无部署事项。 |
| 文档 | changed-and-verified | README补充512³预留池设置及容量边界；current_status补充两轮结果，旧独立调度说法改为历史；性能总表继续为唯一汇总入口。 |
| 规则 | verified-current | 根AGENTS.md仍软链至CLAUDE.md；无算例下级规则；用户已有六个日志删除不纳入提交。 |
| 记忆 | out-of-scope | generated-read-only；仅查询历史保留原则，未获明确更新记忆请求，不写入。 |
| 工作区 | pending | 单worktree；本轮候选及保留范围如下，未清场。 |

## 纠正与验证边界

Q19双方包含q0、使用密度BGK；师兄原程序的18个非静止方向压力模型不能混称标准Q19密度BGK。512³实际Arena高水位34094 MB与保留34102 MB不同，均不等于全设备峰值；只证明本配置关闭输出时可运行。单次窗口4131→4018 MLUPS，不宣布稳定峰值。初始化OOM日志保留，不能当成性能样本。

已核对两轮汇总JSON、原始日志、正式源/程序SHA、相关文档链接、Shell/Python语法。文档无数值路径变化，不新增GPU实验。原始日志尾空格保留原样，不重写证据；文本修改单独通过diff检查。根规则不需增长。

## 清场预览（未执行）

- 唯一候选：runs/temporary/q19_uniform/round1/build/tmp_build_dir，约672 MiB编译对象缓存。构建入口仍以此为缓存目录，但源、配置及job621996内可执行/编译日志已独立保留；删除后复现需要完整重编。获确认后可删除。
- 保留Q19 build/src、config、可执行、flat生成源及验证器、job621996数值checkpoint和全部日志；仍有复现依赖，不删除实验脚本。
- 保留uniform512输入、源、程序、命令及622012/622013/622015失败日志和622016成功结果。submit.sh是提交时快照，不提升为长期入口或迁移历史目录。
- 用户已删除的六个logs/submit文件维持现状，不恢复、不提交；其他历史运行不在本轮清理范围。

pending：Q19长期物理精度/AMR/多GPU、512³重复稳态与输出容量未验证；硬件计数器权限仍不可用。out-of-scope：全仓库结构审查、历史目录批量整理、远端发布及记忆写入。复核现场保留；按neat-freak先汇报后确认，再执行清场。

[性能总表](../均匀网格与AMR性能优化记录.md) · [Q19原始报告](../../runs/temporary/q19_uniform/round1/report.md) · [512³原始报告](../../runs/temporary/uniform512/round1/report.md)
