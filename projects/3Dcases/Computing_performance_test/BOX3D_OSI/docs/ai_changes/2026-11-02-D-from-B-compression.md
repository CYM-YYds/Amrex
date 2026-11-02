# D 阈值短测与 C/D 流场保留

目的：在C基础上再放宽阈值两倍，比较加密效果。仅BOX3D_OSI实验，不改数值代码或配置。复用a5_threshold_probe.sh，以现有末尾参数覆盖lbm.err=0.5 1 2 4，从归档B step128000运行到128128；不新增脚本或重新编译。

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

复现提交命令、完整结果和C/D流场索引位于runs/temporary/D_from_B_compression/。脚本bash -n、输出Header与各层数据存在性、退出码及12条有限值检查通过，git diff --check通过。两组科学复现材料均长期保留。
