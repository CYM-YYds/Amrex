# 汇报 PPT 精修脚本（AI 临时脚本）

用途：在用户的 `docs/自适应网格 LBM 求解器的 GPU 性能优化.pptx` 副本上精修排版与内容，
生成 `docs/自适应网格 LBM 求解器的 GPU 性能优化_精修版.pptx`；原文件不改。
PPT 定稿、不再需要重新生成时可删除本目录。

| 文件 | 职责 |
|---|---|
| `polish_deck.py` | 入口：按原页码调用各页函数、写讲稿、重排顺序、隐藏备用页 |
| `pp_helpers.py` | 文本框、卡片、复制图片/占位符、换版式等共用工具 |
| `pp_slides_a/b/c.py` | 各页改动（背景、现有基础、迁移、OSI、AMR 适配、性能、累加、方腔、总结、目录） |
| `pp_notes.py` | 10 分钟讲稿（约 2000 字） |
| `render_preview.py` | 无 LibreOffice 时的粗略预览：按坐标画图片/色块/文字，不还原公式对象和 WMF |
| `assets/ab_principle.gif` | A–B 两套网格动画，复制自 `runs/temporary/ab_principle_animation/two_blocks/`（`plot_ab_principle.py` 生成） |

```bash
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/ppt_polish/polish_deck.py
.venv/bin/python projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/ppt_polish/render_preview.py <pptx> /tmp/preview [页码...]
```

页上数字均取自 `docs/MLUPS记录.md`（C 阈值 A–B 625774+625855、OSI 624717、累加 626501、均匀 623012）
与 623012 日志的主 Arena 高水位 37166 MiB。若原 PPT 再被手动修改，直接在精修版上继续改即可，
重新运行本脚本会从原文件重新生成并覆盖精修版。
