# 最近10个输出的中心线时间比较

来源：`runs/20261019_124153_job615364`，Re=3200，源版本见 provenance.json。
包含256000步向前10帧：227200、230400、233600、236800、240000、243200、246400、249600、252800、256000。
几何中心线x/H=y/H=0.5，沿z，ParaView 6.2在whshare-agent-1导出，最高读取level2，1024段/1025点。
每帧均1025个有效点、0个无效点，坐标完全一致。256000步与已有独立批处理CSV点列完全相同。

## 结果

- [整体叠加](centerlines_overlay.png)、[近底部放大](centerlines_lower_zoom.png)、[时间诊断](temporal_diagnostics.png)。
- [合并数据](normalized_profiles.csv)：10帧长表，含step、PlotFile时间、t*、z/H、ux/U。
- [探针时间序列](probes.csv)、[两两差异](pairwise_relative_l2.csv)、[延迟差异](lag_distances.csv)、
  [完整分析](analysis.json)。每帧原始CSV、导出元数据与日志均保留。

**存在时间起伏，但这10帧未显示清晰的整条曲线周期复现，不能据此确认稳定周期。**
相邻帧相对L2差为1.83%–3.85%；首尾差5.79%。延迟1/2/3/4/5/6帧的平均差分别约为
2.74%/4.56%/5.27%/5.44%/5.32%/5.53%，没有明显周期性低谷。
最大时间峰峰差为0.0422491 U_lid，位于z/H=0.109375。
局部探针有往复变化，但第一空间主模态仅解释46.15%的波动能量，单一正弦描述也没有充分依据。

## 定义与边界

H=1，U_lid=0.05，z_origin=0；速度归一化ux/U_lid。
时间直接读取各PlotFile Header，t*=time U_lid/H，由88.75到100，每3200步相隔1.25 t*。
总窗口11.25 t*，仅10点；更快波动存在混叠可能，更长周期也可能超出窗口。
有限采样下未见周期复现，不等于证明流动非周期，也不能区分物理非定常与数值误差。
没有作稳态/统计收敛判定。外部参考数据未参与本次时间自比较。

空间差异D(i,j)=sqrt(integral((v_i-v_j)^2) / integral(mean_t(v)^2))，v=ux/U_lid，
积分沿z/H用梯形权重，分母是这10帧平均曲线的平方积分。
延迟k的点为D(i,i+k)，曲线为所有可用对的均值；大延迟可用对较少，9帧延迟只有1对。
峰峰差=max_z(max_t(v)-min_t(v))。固定高度探针按z线性插值，不在时间方向插值。
PCA对减去时间平均后的曲线乘梯形权重平方根后做SVD；分数正负无物理符号约定。
analysis.json保留各模态能量占比及空间线性时间趋势解释的能量比例（31.06%），均为描述量。

## 重绘与复现

从仓库根设置post_dir和result_dir为本算例后处理目录与本结果目录。
逐个step运行既有批处理入口，输入取provenance.json中的run/plt_<step>：

```bash
bash .agents/skills/paraview/scripts/run_paraview_batch.sh \
  --input <绝对PlotFile路径> --output <绝对结果路径>/centerline_<step>.csv \
  --metadata <绝对结果路径>/export_<step>.json --resolution 1024 --max-level 2
```

用根.venv/bin/python调用已有plot_centerline.py，多次传入
`--profile <本目录>/centerline_<step>.csv <step> <step>`，并传
`--height 1 --lid-velocity 0.05 --origin 0 --xlim -0.4 1.05 --ylim 0 1 --output <新图片路径>`。
近底部图用`--xlim -0.32 0.02 --ylim 0 0.2`。更改显示范围不改变差异定义。
诊断数值可从合并CSV按上述公式重建；探针、PCA分数及所有延迟统计已导出，
无需保存一次性分析脚本或重新启动ParaView来改图。

只新增本结果目录，原运行数据、旧图与旧报告未改，没有启动CFD或新提交作业。
