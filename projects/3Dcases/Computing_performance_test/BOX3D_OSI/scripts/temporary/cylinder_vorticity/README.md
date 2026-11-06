# 584000 步圆柱涡量展示

任务专用复现入口；保留脚本、状态、图片和元数据。读取正式运行的
`runs/box_cylinder_native_520000/production/plt_584000`，最大读取层为 L3。
原 `plt_vort` 中的 Vorticity 是模，因此使用速度的 block 局部梯度计算有符号 curl_z。
block 边缘为局部差分，图片用于定性展示，不用于涡量精度验证。
固定红蓝色标 [-3, 3]，超出范围饱和，无额外物理归一化，无外部参考数据。
圆柱按 inputs 的中心 (0.5,0.5)、半径 0.0125 绘制，网格线是 block 外框而非单元线。

从仓库根复现（在 whshare-agent-1 执行）：

```bash
env -u LD_LIBRARY_PATH -u PYTHONPATH -u PYTHONHOME -u DISPLAY \
  QT_QPA_PLATFORM=offscreen .tools/paraview-mpi/env/bin/pvbatch \
  --force-offscreen-rendering \
  projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/temporary/cylinder_vorticity/render.py
```

产物：正式运行 `production/plots/vorticity_584000/` 中的 PNG、PVSM 和 metadata.json。
