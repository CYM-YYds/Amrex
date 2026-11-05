# PPT 框架生成脚本（AI 临时脚本）

用途：为 CCCM2026 MS04 口头报告生成 16:9 中文 PPT 框架，含统一页眉、色板、
示意图（原生形状）、两张由实测数据绘制的图表，以及缺图处的虚线占位框。

- 生成物：`docs/CCCM2026_MS04_AmReX_OSI_汇报框架.pptx`（17 页）
- 运行：仓库根 `.venv/bin/python <本脚本>`
- 依赖：`python-pptx`（已装入根 `.venv`）、`matplotlib`、Noto Sans CJK
- 数值来源：`docs/MLUPS记录.md`、`docs/均匀网格与AMR性能优化记录.md`、
  `docs/osi_algorithm_and_architecture.md`；图表不读日志，数值写死在脚本里，
  改数需同时改本脚本并重跑。

修改 PPT 内容优先直接编辑 pptx；只有需要批量改版式或重画图表时才改本脚本。
任务结束后可删除本目录（assets 下的两张 png 已嵌入 pptx）。
