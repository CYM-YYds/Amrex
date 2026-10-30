# 均匀网格阶段耗时按窗口平均

## 目的与范围

按用户需求将阶段统计收敛为“每步累计、按report_int报告窗口平均”，取消报告步单次快照。只修改BOX3D_OSI三处计时源码、对应验证入口和说明；不增加配置项，不改变碰撞/边界公式、相位顺序或已有GPU同步。

接续工作区已有的三处逐步累计改动及冻结实验，核对源码与已编译快照一致后复用验证，不重复提交实验。用户的inputs、AMR判据/格式、MLUPS文档及后处理修改均未改动或纳入提交。

## 主要改动

- AdvanceOneLayout在支持路径每步记录，复用原事件池与PerfStats窗口清零；不再按绝对报告步稀疏采样，不增加参数或替代推进路径。
- main按窗口实际samples计算1000/samples，输出uniform_stage_gpu(ms/step)和uniform_stage_cpu(ms/step)。GPU主项为碰撞区间和、相位更新、物理边界；CPU提交、MFIter收尾和步末等待作为诊断明细。无样本时eligible=0、samples=0，零值表示未测量。
- 同步和任务顺序保持原样；GPU与CPU视角不可相加，跨stream碰撞区间和也不是推进关键路径。solv/total与MLUPS继续覆盖完整窗口，包含计时自身的额外开销。
- 仍只支持CUDA、单rank、lev0、OSI且uniform_reduce_sync实际生效的路径。窗口平均不扩展成AMR/MPI的GPU事件平均，也不新增2D/DOUBLE/CPU支持声明。
- restart报告仍按绝对步数，但平均分母按本次窗口实际推进步数；尾段不足报告周期仍不额外输出。
- 复用原实验入口，支持指定隔离构建目录，不将用户未提交的AMR改动混入本次候选。README与旧记录的当前用法指向本记录；历史冻结实验不改写。

## 验证

实验：[runs/temporary/uniform_sampling/round3](../../runs/temporary/uniform_sampling/round3/README.md)，作业621696，whshare-agent-82，A100-PCIE-40GB，3D FLOAT。调度终态SUCCEEDED；43个求解子运行及最终分析通过。

- 隔离build目录以HEAD 86f19ba加本次三处计时源码构建，CUDA/MPI编译链接通过；与当前三处源码逐字节核对一致。测试未包含工作区用户AmrCoreLBM_amr.cpp修改，也未覆盖工作区已有可执行文件。基线为round2冻结无事件采样程序；lev0性能不执行AMR判据。
- 5组checkpoint逐值对照（单Fab、多Fab、周期、两rank、动态AMR）的各层valid/active Linf/L1/L2均为0；动态AMR在第32步实际创建level1。锁步无mismatch。不代表长程物理精度或用户当前AMR判据已验证。
- 支持路径每32步报告窗samples=32；report_int=7的常规窗口samples=7；从64步restart，70步首窗samples=6，随后各窗7，报告步为70、77……126，平均分母正确。
- GPU事件均在已有等待后查询完成，值有限、非负；CPU分账之和与平均step在打印误差内一致。不支持模式输出eligible=0 samples=0。
- 性能每种布局5组AB/BA交替，每次1200步，排除首200步，统计201–1200步。每候选性能运行平均覆盖1000个实际推进步，不再是单步快照或稀疏样本。计数与launch_cells在基线/候选之间一致。
- Shell语法、Python语法解析、新增说明路径和git diff空白检查通过；CRLF源码保持原换行。修正分析注释对打印精度的过强假设。

## 计时开销与结果

变化按候选完整total/基线完整total-1计算，正值表示逐步计时增加耗时；不是用GPU/CPU子项相加推算。

| 网格 | 每块大小 | Fab数 | total变化中位数 | 五组范围 |
|---|---|---:|---:|---:|
| 128³ | 128³ | 1 | +1.560% | +1.164% 至 +2.884% |
| 128³ | 32³ | 64 | +3.148% | +2.408% 至 +3.251% |
| 256³ | 256³ | 1 | +0.204% | +0.043% 至 +0.377% |

逐步统计有真实成本，多Fab事件数更多，不能沿用旧稀疏采样的近零开销结论，也不能把这些短测范围当作任意环境的上限。

256³单Fab五组平均：GPU碰撞5.035216ms/step、物理边界0.935398ms/step、相位更新0.006302ms/step；CPU的AdvanceOneLayout平均墙钟5.997733ms/step，已有步末等待0.943153ms/step，事件读取与累计collect=0.003059ms/step。等待与GPU执行重叠，不能重复计入。

原始日志、输入、源码快照、基线/候选程序、命令和汇总保留于实验目录；build中明确标记可删除的编译缓存与重复程序已在核对候选程序哈希后清理，源码与结果不删除。
