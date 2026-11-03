# C 流场 40 帧涡量与 AMR 框线动画

目的：使用已完成C作业624717的流场制作GIF；只涉及BOX3D_OSI后处理，不修改求解器、输入或GUI窗口。

输入为runs/a5_C_t1000_gif/job624717/C/plt_003200至plt_128000，共40份速度场，物理时间25..1000，间隔25。沿用B正式动画的GUI场景和原render_frames.py，另存C_scene.pvsm并替换全部速度路径，未改旧场景或旧驱动。默认最大level2，先三维速度求旋度后切y=0.5，显示mag(Vorticity)*1，无额外归一化、平滑或外部参考；色标0..1固定，超限饱和。AMRLevel整数分类lev0/1/2白/红/绿。

在whshare-agent-1以8个独立单进程pvbatch运行原驱动，2110×1066原尺寸渲染，保留字体和相机。原始PNG因旧驱动固定命名仍带B前缀，但逐帧回执和输入均为C；最终裁剪副本明确改为job624717_C前缀。全部帧统一裁剪(616,0,1493,1066)，不缩放，最终877×1066。

现有工具没有PNG序列合成GIF入口，因此增加scripts/temporary/pack_animation_gif.py，参数化输入、输出、前缀、裁剪、帧数及帧时长；作为此结果复现入口保留。使用全帧共用调色板，40帧每帧500ms、无限循环，一轮20秒。调色板预览不作为原PNG逐像素数值证据。

最终GIF与40张裁剪PNG位于data_post_processing/images/gif-C/；来源哈希和帧序见同名JSON。原始PNG、C场景、批处理命令和日志位于C/figures/animation_C/，全部保留。

验证：bash -n及仓库Python语法解析通过；8个pvbatch任务退出0，40条回执的index/time/input/cells/有限range/AMRLevel检查通过；目视首尾PNG布局；重新打开GIF确认40帧、每帧500ms、尺寸877×1066。git diff --check通过。
