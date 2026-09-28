# BOX3D_OSI 知识收尾（2026-09-28）

- 目的：把最新 Stream 源区域诊断结果同步到现役交接文档，避免继续把物理边界处理误认为首发差异来源。
- 修改：更新 `projects/3Dcases/Computing_performance_test/BOX3D_OSI/README.md` 的状态日期；在 `docs/current_status.md` 增加 job `604902` 的首发点、边界判定和 valid/interface/ghost 源统计。
- 证据：`logs/submit/604902-osi-lockstep-alllevels-64.log`；level 0/1 首发点均 `physical_boundary=0`，BoundaryUncovered 后差异保持；level 0 含 interface/covered 源差异，level 1 含 ghost 源差异。
- 范围：仅完成文档与交接事实同步；未删除历史运行、日志、checkpoint 或候选文档，未修改 Codex 生成记忆。
