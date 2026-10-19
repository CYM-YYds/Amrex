# 退役依赖缺失输入的历史提交脚本

用户在候选清单汇报后确认删除。范围仅为 Computing_performance_test 下 BOX3D_OSI 与 BOX3D_OSI_dev 各四个脚本：

- scripts/submit_osi_stage2_smoke.sh
- scripts/submit_osi_stage3_single.sh
- scripts/submit_osi_stage3_single_array.sh
- scripts/submit_osi_stage7_restart.sh

这些入口均依赖不存在的 config/inputs_osi，没有其他已跟踪脚本调用。
删除前确认文件内容与 HEAD 相同；同步修正 BOX3D_OSI README 与 current_status 的现役描述。
本次跨两个算例的同一清理集统一记录在根目录。历史实现仍可从 Git 获取。

验证：八个文件均不存在；剩余脚本无对应调用；两份文档本地链接及现役入口路径有效；Git 差异空白检查通过。
不启动计算任务。未修改求解器、现役提交入口、测试代码、运行数据或检查点；既有两张图片删除未纳入本次提交。
