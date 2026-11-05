# C阈值OSI/A-B最新优化程序完整重跑

目的：用户要求更新624717/C及625774+625855/C，均使用最新优化代码从零推进到128000步，并保留流场记录。原性能表数值保留为历史，只有新两组实际完成验证后才能替换。

冻结当前工作树src/config和未提交差异，不提交用户原有圆柱/参考尺度等修改；保持cavity、参考速度0.05/长度1、IBM关闭，配置与历史C一致。副本额外补齐上一轮已验证的A-B两个128线程寄存器碰撞调用，OSI沿用现役128线程及坐标复用。正式src不改。全部冻结输入、源文件、差异、程序SHA和实际命令保存于runs/C_optimized_t1000_20261105/。

新增scripts/temporary/C_optimized_t1000/仅负责构建、预检、分段编排和专用最终审计；求解/监护/回传复用既有a5_threshold_probe与checkpoint_io/paired_paths入口。后者只新增IO_LOCAL_MIN_FREE_GIB参数，默认20GiB行为不变；C组32k分段按历史输出量指定12GiB。没有新增求解器状态。

两组同节点单A100串行OSI→A-B，FLOAT/D3Q27/BGK/Re1000、C阈值0.25/0.5/1/2、128³/level2/BF16、max_grid_size128/256/512、regrid32；每3200步统计并输出速度和Q/涡量各40帧，每32000步保留checkpoint共4份。均从零开始，以32k段校验回传后恢复，计划重启各3次；收敛只监测、不提前停止。累计perf.total包含重网格，不含恢复/输出/回传。性能表末窗口与全程累计MLUPS分列。

预检：新程序编译、从零两模式128步的完整valid逐值比较、本地输出与回传SHA通过后自动启动生产。生产后自动核验40完整窗口、场文件/checkpoint头、逐段退出状态和回传清单，输出summary.json/report.md。预检不能证明全程等价或正式收敛。

提交记录：626520因新脚本执行权限缺失未运行，已chmod+x；626521/626522在容量预检处停止，未构建或推进；节点实际本地可用17947688960bytes，避免沿用整批80GiB和通用20GiB门槛，改成32k AMR分段/12GiB后提交626523，构建成功，但预检入口缺PROBE_CHECKPOINT_INT默认键，在应用启动前停止。补齐默认键后提交626524。所有失败材料保留。

626524，whshare-agent-82：构建通过；两组从零128步短回归、各2帧输出及checkpoint回传SHA通过。三层比较56623104/56623104/132046848个DDF值（实际计数以gate_valid_comparison.json为准），不等值及非有限值均0。生产已开始OSI第一个0--32000区段，之后自动续跑并执行A-B全部四段。尚无新128000步结果。原始材料和运行入口见[复现说明](../../scripts/temporary/C_optimized_t1000/README.md)。本次脚本Python语法、Shell语法及链接检查通过；运行验证结果待更新。

冻结程序SHA256：`fad20703fc69d13d3df1b92e4c1b9e5cda80dfe92efe3d0df3d146e18eacf34d`。短回归不代表全程完成。最终完成须有job626524/completed.txt、8条progress记录及summary.json/report.md；正在运行期间不清理构建对象与节点现场。
