# 简化 OSI 原理动画

目的：用户认为 `2026-11-05-osi-principle-animation.md` 中的六阶段动画信息过多，不适合展示，要求按参考图逐步展示“读取 → 碰撞 → 写回”，并体现读取位置在 7×7 区域内最终循环。只涉及绘图工具与文档，不改求解器、输入或实验。

主要改动：重写 `data_post_processing/plot_osi_principle.py`，替换原实现（旧版保留在提交 `31d66f9`，旧输出 `runs/temporary/osi_principle_animation/wall_final/` 不动）。
- 删除逻辑/存储双图、寄存器条、通信检查、相位提交和非周期壁面重建阶段，只保留单张 7×7 网格。
- 每个时间步三帧：读取（虚线框标出 9 个分量位置，越界回绕的位置用橙色框）、碰撞（中心节点向外发散）、写回（与读取同址）。
- 时间步 t…t+6 一个周期，末尾加 t+7 读取帧，说明与 t 相同。
- 方向编号改为参考图编号；GIF 编码继续复用 `pack_animation_gif.py`。

验证：Python AST 解析通过；脚本内核对 p=1…20 时图中位置与 `OsiIndex.H::osi_coord_precomputed` 的寻址逐项一致、首步为相邻节点、周期为 7；GIF 22 帧、每帧 1200 ms，一轮 26.4 秒；人工检查各步读取帧和中心重叠帧的标注可读性。这是图示检查，不是求解器数值验证。

输出：`runs/temporary/osi_principle_animation/simple/`（被 `runs/.gitignore` 忽略，不入库，可用 README 命令重新生成）。
