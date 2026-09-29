# BOX3D_OSI 更新版 596890/596891 运行记录与清理

- 目的：记录更新版 596890 的 A-B 结果，完成更新版 596891 的同参数 OSI 运行，并清理已确认的无独有证据运行目录。
- 修改文件：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/README.md`、`docs/current_status.md`、`docs/MLUPS记录.md`、`docs/osi_algorithm_and_architecture.md`。
- 运行记录：604593 为更新版 596890，604961 为更新版 596891；两者输入快照 SHA256 相同，均完成 128000 步且终态最高 level 为 2。604593 全窗口 `total=5487.08 s`、末窗口 `MLUPS_total=900.04`；604961 全窗口 `total=4017.93 s`、末窗口 `MLUPS_total=1230.97`。两次可执行文件 SHA256 不同，未将其写成严格配对加速比或终态 DDF 等价证明。旧 604594 仅保留 level 0，已标记为退化运行。
- 清理：用户确认后删除 40 个仅含通用运行文件的已结束 `runs/` 目录；保留 55 个含 checkpoint、plot、probe、通信/平均诊断或当前运行证据的目录。运行目录未纳入 Git。
- 验证：`604961` 调度状态 `SUCCEEDED`，日志有完整 40 个性能窗口和 `chk00128000`；Markdown 链接检查仅发现无关的历史 `docs/osi_implementation_plan.md` 引用，已从现役 README/架构页移除对应链接。
