# ParaView连接与B涡量序列知识收尾

范围：本轮ParaView启动器、桥接、动态AMR框线、job622356/B的40帧图像/GIF与对应使用文档。其他求解器、实验与全局配置不扩审、不修改。分支Amrex，收尾开始于c6cff81；只做本地文档提交，未推送或发布Windows文件，不停服、不重启GUI。

## 事实矩阵

| 事实面 | 状态 | 依据与边界 |
|---|---|---|
| 代码 | verified-current | version28、运行时协议2；长连接与退避、一次只读启动预检；生成物与源码一致，64项回归中51通过、13条件跳过 |
| 运行态 | verified-current / pending | 同ID迟到inspect回执已确认：客户端哈希匹配、cs://localhost:11115、B的40个速度PlotFile与时间25–1000、当前t175、AMRLevel节点可见；未重新加载数据、改场景或渲染。新版合并PS1的Windows整套启动及当前GUI视觉验收仍pending |
| 文档 | changed-and-verified | 更新既有bridge/publication/maintenance说明和BOX3D_OSI后处理索引，链接检查通过；现役事实归属见下 |
| 规则 | verified-current | 根AGENTS.md仍为指向CLAUDE.md的有效软链；.claude/skills仍同源到.agents/skills；有效规则链无需新增约束 |
| 记忆 | pending（generated-read-only） | 生成记忆存在旧“远端只支持单进程”和直接编辑生成客户端的表述；当前实现与维护文档已明确MPI能力与生成物边界。本次无明确记忆写入请求，不改生成记忆或创建纠正输入 |
| 工作区 | verified-current / out-of-scope | 单worktree；无本轮工具代码残留。已有config/inputs修改、两个旧PNG删除、用户2DVorticity.pvsm和main_tgv_ke.cu保持原样，不加入本次提交 |

## 现役事实与权威入口

- 连接机制、只读重试、MaxStartups语义及超时处理统一在[GUI桥接说明](../../.agents/skills/paraview/references/bridge.md)；启动器与桥客户端的实现真身仍在对应scripts目录，后处理脚本目录保留兼容软链。无重试单窗口的外部入口连接由9次减至5次，不含节点间转发；不得据此保证入口不再限流。
- GUI场景复用、原尺寸/字号导出、独立帧任务、裁剪与GIF边界统一在[出图说明](../../.agents/skills/paraview/references/publication.md)；实现所有权与协议升级在[维护说明](../../.agents/skills/paraview/references/maintenance.md)。MPI Fetch聚合结构不当作AMR层号，分类警告先查输出有效性，静态块色不作为动态动画方案。
- B序列的现役路径与数值定义在[BOX3D_OSI后处理索引](../../projects/3Dcases/Computing_performance_test/BOX3D_OSI/data_post_processing/README.md)：速度场三维求旋度后y=0.5切片，mag(Vorticity)*1.0，色标0–1饱和显示，无额外归一化/平滑/外部参考。早期原生标量单帧结果与最终序列不混用。
- 本次重新解码40张PNG并逐像素比对原尺寸2110×1066图的固定裁剪区域(616,0,1493,1066)，全部一致，最终877×1066；GIF为40帧、同尺寸、全部500ms、loop=0。GIF SHA256为52d0e78782d5c3e99601f28a035a9b107c159381354b6476b33f2285a7048f44；GIF调色板量化不作为原PNG逐像素证据。
- 历史单进程及16进程AMR字段往返结果见既有dynamic-level-field记录；历史全黑截图仍非视觉验收。当前inspect只证明输入/时间/可见节点，不把它提升为字段数值、图像或求解器正确性证明。

## 盘点、验证与清场预览

执行neat-freak只读盘点脚本，机械枚举618篇Markdown（含历史/测试样例，跳过脚本定义的依赖构建目录），仅阅读受影响文档；根规则73行，路径与同源状态已核对，未改规则或其他项目。对本次修改文档38条本地链接逐一检查均存在；PowerShell语法及预检正文的既有验证边界保留，未因文档收尾重复启动服务。客户端打包检查、完整64项回归及diff检查通过；13项原生PowerShell/ParaView条件测试跳过，不报成全平台通过。

所有正式结果、原始ZIP、旧图备份、GUI状态、裁剪原图、逐任务日志及唯一复现驱动保留。figures/animation_completion、animation_reexport_native、animation_reexport_amrlevel、lev2_outline_diagnosis、level_array_verify均具有历史比较或复现用途，不因早期失败或备份命名判废；本轮无已证明可丢弃的仓库内清场候选，不删除分支/worktree/文件，也无需为继续使用现有GUI重连。

终态：代码已集成、本地门禁通过、文档知识对齐；Windows合并启动器现场验收与当前GUI视觉确认pending，SSH入口断连根因未确定，生成记忆只读，用户无关修改out-of-scope。复核现场保持可用，不宣称整个仓库干净或运行风险已消除。

## 本轮补充复核（2026-11-03）

最终PNG与GIF重新解码核验通过：40张877×1066 PNG均与备份2110×1066原图的[616:1493]水平裁剪逐像素一致，GIF为40帧、每帧500ms、无限循环，SHA256与上述记录一致。最终场景gui_scene.pvsm中可见Vorticity magnitude色标所绑定LUT端点实际为0和1；前一版文档误把早期原生标量单帧0–0.5范围写入最终序列，现按保存场景更正，未改图像或数值。

运行态边界更新：本会话用户已确认修复后画面正常且绿色lev2框线可见；这是当次GUI视觉确认，不代表本轮重新连接或证明断联已修复。后续Layout1配色操作有成功回执，但紧接着snapshot_state超时，用户再次报告vtkSocket.cxx连接错误、ReadPixels告警及桥SSH重试，原因仍未确定。此次不重连、不重放未决命令，也不将只读查询无风险或已消除断联写成现役结论。合并启动器的Windows完整启动验收仍pending。

六面终态：代码verified-current（仅检查生成物一致性，无实现改动）；交付图像/GIF verified-current、GUI连接稳定性pending；文档changed-and-verified；规则verified-current；记忆generated-read-only、未写入；无关用户工作区修改out-of-scope。目录与Markdown机械盘点已执行，复现材料和所有旧图继续保留，无可确定删除候选。此次不推送、不清场。
