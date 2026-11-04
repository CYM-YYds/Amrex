# C阈值 A-B/OSI 耗时柱状图

输入A-B job625774+625855，OSI job624717；均单GPU A100、Re1000、FLOAT D3Q27、C阈值、128³基础网格、max_level2，从静止到128000步/t1000。两段A-B在t250恢复一次。

图为40个3200步窗口的累计秒数，不做cell归一化，不用末窗MLUPS反推耗时。左侧分项，右侧solv/total；regrid_time由ms转s，分项不构成total的严格可加分解。推进total排除初始化、恢复、流场/checkpoint输出及共享盘回传。A-B有重启与I/O跟踪，节点/输出路径及AMR状态不同，不作为严格受控加速比。

生成器复用plot_run_performance.py的窗口解析和字段解析。原入口用于单日志性能概览，不支持本次跨运行分组对照，因此新增本次临时绘图入口，作为唯一生成脚本保留。输入SHA256、统计范围和原始秒数见图片同名JSON/CSV；图内注明来源、范围、单位及比较限制，无外部参考数据。

产物：../../images/C_AB_OSI_t1000_time_comparison.png（预览）、同名.svg（矢量）、.csv、.json。复现命令见command.txt。验证：Python语法解析、40连续窗口覆盖断言、全部阶段及总耗时与已有汇总JSON交叉核对通过；PNG尺寸3080x1496。
