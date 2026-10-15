4. 多 rank 时，进一步完善流水；单卡暂时不用优先做。
   [FillBoundaryOsi() (line 220)](/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/AmrCoreLBM_osi.cpp:220) 已有本地复制与 MPI 重叠、host staging 分块，但仍有几个限制：

- 整批 pack 完成后才投递接收，可以考虑提前投递；
- 所有发送块提交完后才开始处理接收完成；
- 接收按固定顺序 wait()，前面的慢块会推迟后面已到达块的 H2D；
- 最后统一 unpack，尚未形成完整的分块消费流水。
  另外，粗细层用的 [parallel_copy_mpi() (line 297)](/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/OsiCommunication.H:297) 仍是整块 staging 与等待，没有沿用同层通信的分块流水。多卡 AMR 下也值得测，但目前没有证据能承诺收益。
