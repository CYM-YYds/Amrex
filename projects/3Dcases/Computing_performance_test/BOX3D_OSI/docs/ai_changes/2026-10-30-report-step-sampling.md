# 复用report_int进行报告步采样

## 目的与范围

按用户要求移除独立采样间隔参数，减少配置项。仅修改BOX3D_OSI计时触发和日志说明，不改变数值核、执行顺序、GPU事件机制或原有同步依赖；用户已有inputs、AMR、MLUPS文档及后处理修改保持原样。

## 主要改动

- 删除独立间隔的RuntimeParams成员、ParmParse读取及校验，无兼容别名或第二配置来源。
- 详细采样复用performance.report_int，在绝对coarse step能被report_int整除的步触发。复用现有checkpoint.begin_step与level-0推进计数推导绝对步，不新增调度状态。
- 每报告窗口的uniform_sample_gpu/cpu行在支持路径下samples=1，表示报告步单步快照，不是窗口阶段平均/累计；原有perf(s)与MLUPS仍覆盖整个窗口。最后不足一个周期的尾段不额外采样或报告。
- restart按绝对步对齐，首个不完整窗口也在其报告步采样；不支持的模式仍输出eligible=0 samples=0。
- README同步当前用法，首次实现记录保留历史结果并标注后续替代；复用现有实验入口与分析器，历史冻结脚本、日志和数据不改写。

## 验证

实验目录：[runs/temporary/uniform_sampling/round2](../../runs/temporary/uniform_sampling/round2/README.md)。旧程序默认无事件采样，作为冻结独立基线；新程序按report_int自动采样。同节点交替、同输入、同网格布局，排除首200步，统计201–1200步。该对照是两个版本的整体影响，不将差异全部归因于事件API开销。

- 作业621694在whshare-agent-50成功结束（SUCCEEDED），通过算例GNUmakefile完成3D FLOAT CUDA/MPI编译；25个求解子运行和最终分析均正常完成。
- 5组checkpoint对照（单Fab、多Fab、周期、两rank和动态AMR）的valid/active Linf/L1/L2均为0，AMR实际在第32步创建level1；OSI/A-B锁步检查无mismatch。不支持的AMR、多rank和锁步模式输出eligible=0 samples=0。
- report_int=32的支持路径每窗samples=1；report_int=7的64步运行在7、14……63步各采样一次，末尾第64步不额外输出。从64步restart到128步，在70、77……126步各采样一次，首报告窗70包含6个实际推进步、1个样本。
- 全部事件查询已完成，计时有限且非负；CPU各分账之和与sample.step一致。性能每运行排除首窗后纳入5个报告步样本，而不是原先10个间隔样本。
- 与冻结无采样程序的3组交替total耗时变化：128³中位数-0.135%，范围-0.137%至+0.247%；256³中位数-0.072%，范围-0.088%至+0.188%。差异随顺序波动，未显示稳定额外耗时，不能当成加速或任意环境的开销上限。
- Shell语法、Python语法解析、README/记录路径以及git diff空白检查通过；CRLF源码保持换行，按core.whitespace=cr-at-eol检查。未新增2D/DOUBLE/CPU构建或长程物理验证。
- 原输入文件未修改；独立参数在当前src及本次实验入口中无剩余引用，旧实验的冻结源码与记录按历史证据保留。
