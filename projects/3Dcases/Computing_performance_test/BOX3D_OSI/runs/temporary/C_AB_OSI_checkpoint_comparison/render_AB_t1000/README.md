# A-B C阈值终态涡量截图

输入job625855/C_mode0/plt_128000，物理时间t=1000。复用job622356保留的render_frames.py与job624717 C_scene.pvsm，只替换40个输入路径为job625774前段及job625855后段，在whshare-agent-1单rank pvbatch渲染第39帧。用户当前GUI与2DVorticity.pvsm未修改。

速度三维curl后取y/H=0.5切片，显示涡量模，倍率1，固定色标0至1（超范围饱和）；视角、字体及AMR轮廓沿用原图：lev0白、lev1红、lev2绿、线宽3。原始2110x1066，沿用原裁剪框(616,0,1493,1066)，输出877x1066，无缩放。

最终图data_post_processing/images/job625855_AB_C_t1000_vorticity.png；保留状态、命令、原图、render.log和export.json。日志验证时间、输入、单rank主机、非空单元、有限涡量范围和三层轮廓。驱动原始文件名中B/job622356为历史硬编码，不代表本次输入。图片只是可视化，不替代数值误差或收敛验证。
