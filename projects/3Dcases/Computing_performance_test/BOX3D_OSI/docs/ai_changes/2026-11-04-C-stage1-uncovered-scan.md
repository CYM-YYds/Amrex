# C 阈值 A-B/OSI 从零开始的阶段一扫描

目的：按stage-aware-amr-diagnostics先定位首个uncovered valid分歧，不以covered checkpoint差异或提前展开的阶段诊断判根因。

范围：基于长跑冻结20261029源码建立独立实验副本；生产源码与原可执行文件均未修改。prepare_phase1.py复用原比较器及推进函数，保留SwapLevel，仅将锁步比较调度改为初场和每32个完整粗步末；增加比较区域内非有限值检查和topology/mask/phase记录。补丁、源码、程序与构建日志保存在runs/temporary/C_AB_OSI_stage_review/phase1_build。同步扩展临时本地输出入口的输入设置文件/模式/输出参数，短测试无正式收敛点时跳过绘图，避免退出后漏跑另一组。

验证：Python语法、Shell bash -n、CUDA+MPI FLOAT独立构建通过。job625934两组从零到1024步，离线共同uncovered valid共347701248个DDF值全部一致，covered mask及Box顺序一致；全valid的差异仅在covered粗层。job625953从零到128步，在0/32/64/96/128共12次逐层uncovered比较linf=0、退出0，确认未展开阶段二。长扫描job625951从零到6400步已提交、尚未完成。

边界：锁步拓扑和mask共享，不能据此宣称两次独立加密选择一致。原正常重构有限值保护保留，非A-B/OSI全valid逐值比较。首轮job625948因诊断副本漏SwapLevel产生无效差异，已修正并以128步校验通过；失败源码/程序/日志和补丁保留，不作为生产差异证据。此前多阶段job625927已停止并保留，不将其视为按技能完成定位。尚未锁定长跑首个active分歧或证明数值等价。
