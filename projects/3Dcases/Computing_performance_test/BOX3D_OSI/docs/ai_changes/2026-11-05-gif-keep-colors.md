# GIF 合成器保留指定颜色

目的：OSI 原理动画的 GIF 中看不到红色错误分量。`pack_animation_gif.py` 先把每帧缩到 128×128 再量化共享调色板，细箭头在缩略图中几乎消失，红色没有进入调色板（PNG 第 3 帧 1672 个红色像素，GIF 中为 0，全部变成灰蓝 (106,119,133)），蓝色也被替换。

改动：`pack_animation_gif.py` 新增可选参数 `--keep-colors RRGGBB ...`，把指定颜色固定放在调色板前部，其余位置沿用原缩略图量化结果；合成后用 numpy 逐帧核对这些颜色的像素在 GIF 中保持原值，JSON 记录 `keep_colors`。`plot_osi_principle.py` 调用时保留蓝、红、深色框线三色。README 的 GIF 一节补充说明。

影响范围：省略 `--keep-colors` 时行为不变；已用同一输入分别以旧版与新版合成，GIF 逐字节相同，涡量等既有动画不受影响。

验证：两个脚本 AST 解析通过；重新生成 `runs/temporary/osi_principle_animation/boundary_6_4/`，第 0、3、4 帧红、蓝像素在 GIF 中逐像素一致，合成器内置核对全部 36 帧通过；人工查看 GIF 第 3 帧红色箭头与框正常显示。之前用旧合成器生成的 `simple`、`node_4_4`、`node_5_5*` 的 GIF 中蓝色同样偏灰，未重新生成。
