# BOX3D_OSI 知识收尾

- 目的：把完整 Q 路径、双 GPU 运行证据和未完成边界同步到现役文档，避免旧的分批处理和“仅编译验证”描述继续冒充当前状态。
- 修改文件：`docs/current_status.md`、`docs/osi_algorithm_and_architecture.md`、`README.md`。
- 主要改动：记录提交 `c5601f1`、作业 `606200`、2 ranks/2 GPUs 四组矩阵及有效区域逐点结果；明确完整 Q 临时 `MultiFab` 仍可能占用显存，并列出单 GPU、device-direct、多节点和长程验证边界。
- 规则与记忆：根 `AGENTS.md` 仍软链到 `CLAUDE.md`；本次未修改生成记忆，也未删除日志、checkpoint、runs 或其他残留。
- 验证：重新检索现役文档中的已退役分批符号；保留带日期的历史记录。代码和运行证据沿用提交 `c5601f1` 与 `606200` 的已保存日志，未把文档审计当作新的数值验收。
