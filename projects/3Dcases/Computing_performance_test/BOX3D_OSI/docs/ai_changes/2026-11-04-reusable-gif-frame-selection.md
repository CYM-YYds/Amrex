# GIF帧段选择长期入口

目的：下次直接从PNG序列制作前N帧或中间帧段，无需临时截取已有GIF。
范围：BOX3D_OSI后处理，不修改流场、GUI或求解器。
将已有pack_animation_gif.py提升至data_post_processing长期维护，
原scripts/temporary路径保留相对软链，保护历史命令；不建立第二套合成逻辑。
新增--start-frame（从0计数）、--frames（省略则取全部剩余帧），
沿用--crop和--duration，保留每帧源索引、哈希、原尺寸、裁剪和时长报告。
预检编号、帧段范围和输出冲突；GIF时长要求10ms整数倍以避免静默取整。
README加入入口索引、当前C前15帧命令及新布局需重新核对裁剪的说明。
验证：Python AST；新入口真实40帧输入选前15帧成功，旧软链选源索引10..12成功；
重新解码核对15/3帧、1006×935、500/200ms和来源索引，越界请求在输出前拒绝。
git diff --check通过。新15帧结果在images/gif-C-viridis-first15/，
中间帧段验证材料在runs/temporary/gif_frame_selection/。旧GIF及完整序列保留。
