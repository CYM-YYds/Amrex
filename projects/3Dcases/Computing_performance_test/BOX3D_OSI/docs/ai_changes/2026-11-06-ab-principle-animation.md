# 传统两套网格（A-B）示意动画

目的：汇报中与 OSI 原理动画对照，展示传统两套网格算法。用户要求两个 3×3 网格块：左块中心碰撞后向外迁移，右块周围碰撞后相关分量迁移到中心。只涉及绘图工具与文档。

主要改动：新增 `data_post_processing/plot_ab_principle.py`，同步展示左右两块的“碰撞 → 迁移”两帧。现有 `plot_osi_principle.py` 只画 7×7 单环，职责不同，故新增入口；格点背景、箭头几何、编号、颜色与字体环境直接从该脚本导入，以保持同一组汇报图风格一致。为此 `plot_osi_principle.lattice()` 增加可选网格尺寸参数 `n`（默认 7），已核对 OSI 动画第 0、9、18 帧 PNG 与修改前逐字节相同。GIF 复用 `pack_animation_gif.py --keep-colors`。README 增加索引与用法。

验证：AST 解析通过；脚本内核对左块迁移目标 `c+e_q`、右块来源 `c−e_q` 各为 8 个不同的相邻节点；合成器核对蓝、浅蓝、深色框线在 GIF 中逐像素保留；人工检查两帧版面与编号。输出 `runs/temporary/ab_principle_animation/two_blocks/`（被 `runs/.gitignore` 忽略）。教学示意，不是求解器数值验证。
