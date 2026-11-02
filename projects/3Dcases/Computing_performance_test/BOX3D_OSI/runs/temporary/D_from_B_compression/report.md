# C/D 加密效果与流场入口

D job623748成功，从同一B检查点推进128步至t=1001；阈值0.5/1（完整数组0.5/1/2/4）。保持C相同的冻结程序、BF16、grid_eff0.7和regrid_int32。未修改用户inputs。
统计各层分配valid单元，含covered粗层、不含ghost，压缩基准512³=134217728。

| 模式 | lev0 | lev1 | lev2 | 总数 | 节省率 | 压缩倍数 |
|---|---:|---:|---:|---:|---:|---:|
| B | 2097152 | 8110080 | 20832256 | 31039488 | 76.87% | 4.32 |
| C | 2097152 | 3768320 | 11866112 | 17731584 | 86.79% | 7.57 |
| D | 2097152 | 2240512 | 5292032 | 9629696 | 92.83% | 13.94 |

## ParaView 打开路径（全部保留）

两组时刻均为t=1001。plt_含ux/uy/uz，plt_vort_含Q/Vorticity；读取最高level2即可检查层级及加密位置。目录B表示检查点来源，实际模式以本表与overrides末尾阈值为准。

- C plt_128128: `/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/C_from_B_compression/job623707/B/plt_128128`
- C plt_vort_128128: `/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/C_from_B_compression/job623707/B/plt_vort_128128`
- C chk00128128: `/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/C_from_B_compression/job623707/B/chk00128128`
- D plt_128128: `/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/D_from_B_compression/job623748/B/plt_128128`
- D plt_vort_128128: `/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/D_from_B_compression/job623748/B/plt_vort_128128`
- D chk00128128: `/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/D_from_B_compression/job623748/B/chk00128128`

D每次重构的各层有限值检查通过，guard和rank退出0。仅1个物理时间的网格响应，未验证长期收敛与精度；节省率不是显存节省率或加速比。原C、B数据和B长续跑不改变。
