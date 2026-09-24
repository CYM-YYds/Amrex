# BOX3D_OSI 锁步参考态细层 ghost 推进修正

- 目的：让 `verification.osi_ab_check=true` 时的 A-B 参考态与独立 A-B 模式采用相同的粗到细 ghost 填充、碰撞和迁移范围，消除 step 32 细层 `Stream` 的假报警。
- 修改文件：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/AmrCoreLBM.cpp`、`projects/3Dcases/Computing_performance_test/BOX3D_OSI/docs/osi_ab_stage_evidence_2026-09-24.md`。
- 主要改动：锁步模式在 OSI 粗到细填充后也调用 A-B 的 `FillDdfGhostFromCoarse()`；A-B 参考态的 `Collide`、`Stream`、`SwapLevel` 改用 `nghost`，与独立 A-B 时间推进一致。生产模式的调用路径不变。
- 验证：`GEN_CCDB=0 ./scripts/compile.sh --no-submit` 成功；job `603503`（单 rank、最多 32 步）在第 32 步 level 1 的两个细步中，Initial、Collision、Communication、Stream、Boundary、Swap 全部报告 `linf=0`，程序正常结束。旧 job `603490` 在首个细步 `Stream` 后报 `3.8184455009127732e-05`，该报警现未重现。证据保留在 `projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20260924_231109_job603503/`。
- 限制：锁步参考态后续的界面平均、完整平均及重网格路径尚未全面对齐；本次仅验证到 step 32，不能据此解释独立 OSI 1000 步运行的 NaN。
