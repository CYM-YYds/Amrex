# Roofline 小测试

目的：验证当前 BOX3D_OSI 是否能采集 GPU 显存流量及 Roofline 数据，先用单 GPU、256³ 单层网格缩小采集范围。

范围：新增本轮临时提交/分析入口及独立实验记录；旧入口绑定历史冻结程序，因此本轮独立冻结当前构建产物，保留旧实验不回写。求解器、正式配置和用户已有改动不变。

结果：当前增量构建检查成功。job624743 三次 100 步短测退出 0，排除各轮最初 20 步后，推进平均 4.630 ms/step，碰撞 CUDA 事件平均 3.645 ms/step。按 D3Q27 FLOAT 216 bytes/cell 计算的逻辑有效带宽为 994.2 GB/s；该估算不是实测 DRAM 带宽。三轮最终有限性检查完成，保留第三轮场和 checkpoint。

Nsight Compute 退出 1，报告 ERR_NVGPUCTRPERM，节点 RmProfilingAdminOnly=1；没有 Roofline 点或实测带宽利用率，需管理员开放授权后重试。首轮624742仅因脚本执行权限失败，已纠正，原始日志保留。

验证：Shell bash -n、Python AST 语法解析；分析程序检查三轮退出码、精度、最终状态有限性、窗口数及发射 cell 数，并复算摘要。文档相对链接和输出 Header 存在性已检查。未改数值路径，不宣称逐值等价、长期稳定性能或 AMR 代表性。

实验用途为临时诊断；程序、配置、源码、命令、日志、场、checkpoint及唯一复算脚本均保留。详见[本轮结果](../../runs/temporary/roofline_smoke/round1/report.md)。
