# Re=1000 三维数据 ParaView 提取及对比流程

- 目的：验证 job613495 最新三维输出可正常处理，使用 Re=1000 的 Fig.18(a)，不误用 Re=3200 参考。
- 范围：新增 data_post_processing/extract_run_centerline.py，不覆盖旧 CSV 或图像，不改物理模型。
- 实现：自动选择运行目录中编号最大的 plt_；通过共享控制桥调用 ParaView 中心线导出，生成独立结果目录。分析校验真实几何 bounds、vtkValidPointMask、有限值、内部采样缺口，按 H 和 U_lid=0.05 归一化，与本地 digitized Fig.18(a) 数据对比并输出误差 CSV、PNG 和带哈希的报告。旧 160000 步 CSV 只作曲线形状参考，不用于时间收敛证明。
- 验证：Python 语法、仓库路径、模拟 CSV 的重复点聚合、无效端点剔除、内部缺口拒绝通过；暂存 diff 检查通过。
- 待验证：当前 GUI 加载新版桥后，实际提取 job613495/plt_256000 并核对结果。未安装 yt，也未使用其他 Python 环境运行后处理。启动过的依赖安装进程已停止。
