# 当前GUI的C数据Viridis动画

目的：按用户当前GUI场景制作C/job624717动画，保留用户拖动后的方向坐标指示。
输入：40份速度PlotFile，step3200..128000、time25..1000、level0..2。
场景：Viridis涡量模，mag(Vorticity)*1.0，固定范围0..1（超限饱和），无外部参考。
复用现有render_remaining.py，增加可见源选择视图、输出前缀及方向轴viewport参数，
禁用首次Render相机复位；未重建GUI管线。复用pack_animation_gif.py，
去除旧2110宽硬编码，验证一致尺寸及有效裁剪范围。

导出：whshare-agent-1上8个独立单rank pvbatch，1834×1066；
恢复GUI方向轴viewport [0.13904034896401346,0,0.2839205232559527,0.25]。
首帧确认后补齐39帧；按全帧内容边界并集加16像素余量裁剪
(367,77,1373,1012)，最终1006×935，不缩放；坐标箭头、轴文字和完整色标均保留。
全帧共用256色调色板，40帧、每帧500ms、无限循环，一轮20秒。

验证：Python AST及git diff --check；桥接现有66项unittest（15项跳过）；
40条回执的输入/索引/物理时间/非空单元/有限值范围检查通过；
40帧原尺寸解码，方向坐标区域逐像素一致；目视首尾裁剪PNG；
重新打开GIF验证40帧、尺寸、全部帧时长和循环属性通过。
此为可视化导出验证，不代表求解器正确性或收敛。

输出：data_post_processing/images/gif-C-viridis/job624717_C_viridis.gif，
同目录保留裁剪PNG与来源哈希JSON。场景、坐标viewport、冻结脚本、命令、
逐帧日志、验证报告及原PNG在
runs/a5_C_t1000_gif/job624717/C/figures/animation_C_viridis_current/。
位置不正确的早期frames_raw和相机复位试图frames_axes_fixed亦保留，未用于最终GIF。
用户当前GUI及已有旧GIF不覆盖。
