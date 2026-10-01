# BOX3D_OSI 编译单元拆分知识收尾

- 目的：让现役导航、状态和架构文档与提交 `23b92b7` 的源码拆分及 job `606276` 的实际证据一致。
- 主要改动：更新五个 `AmrCoreLBM` 实现文件的职责链接；移除架构页对已删除持久 `osi_sync_buffer` 和缓存 fallback `Array4` 的描述；区分 staged/active/covered checkpoint 对比，并标出单 GPU、重启、多节点、device-direct 和长程验证为 pending。
- 证据：CUDA + MPI 编译、OSI index shell 检查、606276 的 2-rank/2-GPU 四组 step-64 作业均已完成。运行目录保留输入、日志、checkpoint 和可执行文件 SHA256；当前专项脚本的 `source.diff` 未覆盖新增拆分文件，复现需同时使用提交 `23b92b7`。
- 待处理：四个 `submit_osi_stage*.sh` 仍引用缺失的 `config/inputs_osi`，单 GPU 和 restart 入口因此保持 pending。
- 规则与残留：根 `AGENTS.md` 仍软链至 `CLAUDE.md`；未修改生成记忆，未删除日志、checkpoint、runs 或 `/tmp/box3d-tu-refactor.yV6P7u` 中的回退现场。
