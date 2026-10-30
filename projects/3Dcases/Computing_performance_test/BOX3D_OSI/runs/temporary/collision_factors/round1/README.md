# 现有AMReX D3Q27碰撞因素隔离实验（临时）

原模型、边界、SoA/Fab、RDC及相位环语义不变；同程序host分派四模式：0原256线程，1仅128线程，2仅复用每单元九个轴坐标，3组合。坐标候选复用现有osi_cell_coordinates/load_ddf_cached/store_ddf_cached，不添加永久字段、缓存或第二套公式；试验仅无mask lev0 OSI分支。旧AMR/DOUBLE坐标复用实验曾变慢，不能外推到当前FLOAT均匀网格。

隔离build来自当前工作树，包含用户当时已有修改；不改正式src/config。使用当前GNUmakefile配置与AMReX缓存重编本算例源码。冻结621699程序作外部对照，以mode0检查重编和host分派影响。先全DDF验证，覆盖多相位跨环、单/多Fab、周期/非周期、restart及256³实际尺寸；全部valid/active范数为0才进入同节点25次换序性能运行。每次1200步剔除首200步，按有效256³更新量/累计total计算。

保留输入、源码、限定补丁、构建/运行命令、程序哈希、日志、数值检查与报告。build/tmp_build_dir是可删除构建缓存；清理前另行核对。没有硬件计数器，不以静态寄存器数量或有效字节流量直接判定实际带宽/occupancy。
