# A-B / OSI t=1000 checkpoint 比较

本次临时诊断；保留脚本、命令、结果、源checkpoint和全部运行证据。没有修改求解器或checkpoint。

输入：job625855的chk00128000（A-B，由job625774在t=250重启）；job624717的chk00128000（OSI连续运行）。均step128000、t1000、finest_level2、C阈值、FLOAT D3Q27。

冻结WriteCheckpoint源码确认OSI先DecodeOsiValid写canonical f_old，A-B直接写f_old。A-B文件带2层ghost，OSI文件ghost为0；因此不能直接比较文件哈希。比较器从f_old_H读取valid boxes、FAB头读取实际存储box/分量数，使用内存映射、按相同level共同物理索引逐值比较27个分布函数，排除ghost。相对L2分母为OSI的共同区域分布函数L2。包含被细层覆盖的粗层valid，不是复合AMR速度误差，也未用阈值判定通过。

结果：level0、2的boxes（含顺序）相同；level1两者14个box，但A-B有3207168个valid cell，OSI有3448832个，其中OSI独有241664个。共同区域相对L2差异分别2.36539985e-5、2.71486050e-5、1.41760274e-5；最大绝对分布函数差异7.32764602e-5、6.13108277e-5、2.43186951e-5。全部共同区域比较460947456个值，未发现非有限值；存在逐值差异，不能归因为单纯存储布局。

边界：没有比较两者独有覆盖区域；不同重启历史与自适应网格演化混在一起，尚未定位根因或证明数值等价。若定位实现问题，需同一checkpoint、固定网格、短程阶段对照。
