# C 阈值 A-B 本地输出续跑

目的：完成用户要求的物理时间t=1000长跑，绕开共享盘直接VisMF写入EIO。

改动：复用paired_paths.py，增加显式IO_RESTART_ROOT/IO_RESTART_STEP参数，未指定时行为不变。读取job625774已校验chk00032000，从t=250续跑至t=1000；每3200步保留速度/涡量场、32000步checkpoint、100ms显存采样，节点本地输出后校验回传，不修改求解器或历史输入。冻结快照与共享checkpoint通过只读使用的符号链接组合，不覆盖源数据。

验证：Python语法解析、Shell bash -n、git diff --check通过；checkpoint Header步数为32000。job625855已提交，完整恢复推进和回传尚待运行验证。日志目录runs/C_AB_t1000_local_continuation；前250时间原始结果保留于runs/temporary/C_AB_local_output_validation/job625774。
