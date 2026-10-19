# 历史后处理复现

[plot_transfer_cost_pies.py](plot_transfer_cost_pies.py) 保留内嵌的三组计时常量和原饼图算法，
输出仍为算例 `docs/transfer_cost_pies.png`。从仓库根使用 `.venv/bin/python` 调用本文件路径。

该脚本只注明了“64 个统计窗口”，原始日志、确切步数和来源版本尚未定位；本次没有补造出处。
图内明确标注这些缺口及占各计时小计的比例归一化方式。
它可重绘历史常量图，但不能用来证明当前性能，也不能由重绘成功推断历史实验可完整复现。
日常性能日志绘图入口在上级 `plot_run_performance.py`，本次没有迁移或删除历史图片与数据。
