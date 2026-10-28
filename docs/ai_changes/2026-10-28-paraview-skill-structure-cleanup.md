# 整理 ParaView skill 的脚本结构与文档

目的：针对结构评审提出的问题，减少"新增一个动作要改多处"、"出图版式与数据管线混在一起"
和"投递器职责错位"，不改变任何桥操作的数值行为与协议。

- `build_paraview_client.py` 同时生成并检查桥客户端与独立 `repair_amr_gradient.py`；
  投递器移除 `export_gradient_script`（`client_manifest` 保留，现役 PS1 通过它查询能力）。
- 投递器整理为 `main()`，命令行选项由 `BRIDGE_ACTIONS` 动作表描述；选项白名单由该表派生。
- 运行层 `execute()` 改为 `OPERATIONS` 分发表；`inspect` 抽成模块级 `inspect_state`。
  它不能做成新增实例方法：热更新时新版 `execute` 会作用在旧版实例上，旧类没有该方法。
- `publication_vorticity` 与 `add_axis_title_view` 拆到 `paraview_publication.py`，函数体与拆分前
  AST 逐一一致；构建器按 `OPERATION_MODULES` 嵌入多个模块并检查跨模块重名。
- 导出分辨率在 `publication_vorticity` 内只计算一次，回执与 `SaveScreenshot` 共用。
- `BRIDGE_VERSION` 升至 17。文档按主题拆出 `references/publication.md`，历史验证链接并入
  `maintenance.md`；维护说明明确热更新哈希只防版本漂移与下载不全，不是防篡改。

验证：普通回归 42 项通过（9 项跳过），新增 `test_sender_cli` 覆盖动作表与客户端能力一致、
各动作命令字段、必填项错误。投递器对 16 组命令行用旧版（HEAD）与新版对照，投递的命令内容
和退出码完全一致。用 HEAD 的 v16 客户端模拟运行中的旧桥，热更新到新客户端成功，
`inspect` 除版本和源码哈希外完全相同。编译节点 pvpython 回归 9 项中 7 项通过、2 项因未指定
MPI 进程数/真实 PlotFile 跳过。文档相对链接与锚点检查无失效。

未验证：Windows GUI 现场热更新（需用户本机执行 `reload_client`）；MPI 多进程与真实 PlotFile
下的出图路径本次未重跑。
