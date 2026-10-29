# 当前 GUI 的 FXAA 警告排障（2026-10-29）

目的：用户导入 B 组后持续报告 vtkOpenGLFXAAFilter 无法取得 shader program，要求直接桥接排障。改动仅涉及仓库公共 ParaView 桥，不修改 BOX3D_OSI 求解器或用户数据。

## 现场与处理

当前连接 cs://localhost:11115、Windows GUI、三视图 ID 10905/12295/13262。热更新后实际检查显示三者代理 UseFXAA=1、客户端视图开启、主 renderer 开启，叠加 renderer 关闭。此前手动命令没有在检查时留下关闭状态；具体原因未确定，不归因为 B 数据损坏。

新增白名单动作 disable_fxaa，复用既有 view_id 选项，默认处理全部现有 RenderView。它关闭代理属性、客户端 vtkPVRenderView 和主/叠加 renderer，随后重绘。回执包含前后实时状态、仅本次客户端渲染的新消息和 FXAA 警告标志。消息收集器在 finally 恢复原 GUI 输出窗口，不清除历史错误。

inspect 复用同一 render_diagnostics 读取真实状态，避免仅检查代理属性。该 helper 的职责是统一三处 FXAA 状态的观测，不引入缓存或持久状态。现有背景、涡量和排版操作不包含此职责，因此增加独立、受限渲染操作；未开放任意 Python 投递。

桥客户端由打包器重建，版本升至 20，在当前窗口成功热更新。现有管线对象与三个视图保留。逐视图执行后，三者代理、客户端视图及两类 renderer 均关闭；每次重绘捕获消息为空，没有新的 FXAA 警告。独立 inspect 再次确认全部关闭。操作不定位原始 shader 编译/驱动故障，不能据此声称驱动兼容性已修复；未来新建视图仍可能默认启用 FXAA。

## 验证

- 打包及 --check 通过；现有 unittest 共 47 项，10 项按环境跳过，其余通过。
- whshare-agent-1 私有 ParaView 6.2 实测：构造代理为 0、客户端视图关闭但底层主 renderer 仍开启的状态，新操作将底层关闭并成功重绘，无新 FXAA 警告。
- 重绘故意抛出异常，确认恢复原 vtkOutputWindow；未让临时消息收集器留在 GUI。
- 真实 Windows GUI 三视图执行及后续 inspect 通过，源和视图身份保留。该现场的前置状态是三者均开启，不把独立构造的不一致场景当成该现场根因。
- 文档链接、Python AST 与 git diff --check 通过。未重跑求解器：该变更只影响可视化抗锯齿。

用法见 [桥接说明](../../.agents/skills/paraview/references/bridge.md#fxaa-渲染警告)。回执保留于 [修复记录](../../projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/a5_threshold_fields/job621375/fxaa_repair.json) 和 [后续检查](../../projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/a5_threshold_fields/job621375/fxaa_post_inspect.json)。用户原有输入、求解器、MLUPS 文档及 2DVorticity.py 不纳入本次提交。
