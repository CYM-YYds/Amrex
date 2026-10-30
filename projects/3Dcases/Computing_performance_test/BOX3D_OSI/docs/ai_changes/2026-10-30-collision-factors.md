# 均匀D3Q27碰撞线程与坐标复用因素测试

目的：在现有AMReX同数值模型/同NEE边界下，隔离128线程、每cell轴坐标复用及其组合；纠正将原18分量程序吞吐直接当D3Q27实现效率上限的比较口径。

仅实验副本修改Kernels.H模板与无mask lev0 OSI碰撞启动；复用既有osi_cell_coordinates/load_ddf_cached/store_ddf_cached，保持BGK公式、求和、Fab回绕、边界、同步与RDC不变。新增临时生成/构建/分析入口用于冻结源与先回归后计时；不增加正式参数、状态或求解器路径。

job621730完成CUDA构建、24次数值应用（20次全DDF checkpoint比较均valid/active L1/L2/Linf=0）与同节点25次换序性能运行。现有模式约2822 MLUPS，仅128线程3122，仅坐标复用2988，组合3685；组合total下降23.43%、吞吐提高30.60%。四核168寄存器无spill；坐标复用静态SASS条目少15.14%。详细证据与范围以[实验报告](../../runs/temporary/collision_factors/round1/report.md)为准。

验证包含64³单/多Fab周期/非周期、相位跨环、restart及256³真实网格；未覆盖AMR、多rank、其他精度/维度及长期物理精度。没有硬件计数器，不声称已测实际occupancy或DRAM瓶颈。正式代码未整合，用户已有修改保留；实验含当时工作树修改，不称干净HEAD验收。

Shell/Python语法、限定补丁、文档路径、原始退出码和diff空白检查通过。首次621729因可执行位缺失未进入构建，修正后重提；失败日志保留，全部材料不清理。
