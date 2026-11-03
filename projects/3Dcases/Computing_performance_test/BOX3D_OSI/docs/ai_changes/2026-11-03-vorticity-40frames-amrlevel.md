# B算例40帧涡量图重新导出

目的：旧序列中自动vtkBlockColors不能可靠区分lev1/lev2，按用户确认的AMRLevel方案重导全部40帧。

输入：runs/a5_B_three_plot3200/job622356/B/plt_003200至plt_128000，时间25至1000，间隔25，AMR 0–2层。从当前GUI保存场景，选用具有Vorticity magnitude水平色标的优化布局，沿用GUI中三维速度旋度、y=0.5切片和mag(Vorticity)*1.0，不额外归一化、不使用外部参考数据。2110×1066原尺寸渲染，不缩放字体；AMRLevel整数字段分类白/红/绿，线宽3，隐藏旧框线和层级图例。

执行：复用此前补帧驱动的任务复现副本，增加当前布局选择与每帧层级校验。whshare-agent-1上20个独立单进程pvbatch任务，每任务两帧；不操作GUI。0006试导绿色lev2正常。全部任务成功，40条回执输入/时间/单元数/有限范围与层级字段检查通过，40张完整解码、尺寸与红绿像素检查通过；0006与0039目视检查通过。替换后SHA256逐张核对一致。图像与字段检查不构成求解器数值正确性证明。

结果：data_post_processing/images/job622356_job622356__B__victory.0000.png至.0039.png。旧图40张、SHA256清单、GUI状态、复现脚本/命令、逐任务日志、试导图和completion_report.json均保存在runs/a5_B_three_plot3200/job622356/B/figures/animation_reexport_amrlevel/。该目录为正式结果与复现材料，保留；未覆盖用户原始2DVorticity.pvsm及无关修改。
