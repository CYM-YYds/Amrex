# OSI 固定使用直传，移除可选 canonical 回退

## 目的与范围

仅修改 BOX3D_OSI。删除与现有 raw 地址直传重复的生产回退，减少路径分叉与全层临时分配。

## 主要改动

- 粗到细 ghost 搬运固定使用单 rank 缓存 tag 或多 rank CPC pack/unpack。
- 接口平均固定直接写回粗层 raw 地址，删除全层 transfer_canonical。
- 同层通信固定使用 raw 本地复制及 OSI-aware MPI，删除 decode/FillBoundary/encode 回退及其死缓存、tag 类型。
- OSI 新层初始化固定使用 ParallelCopyOsi，删除可选 DecodeOsiValid 初始化分支。
- 删除三个 direct 布尔成员。旧参数值 1 暂时兼容，值 0 明确报错；当前 inputs 不再设置。
- 保留实际算法和检查点/诊断所需的 canonical 工作区，包括完整 valid restriction、插值、A-B 参考态。
- 更新测试矩阵，删除 fallback 组，增加单 rank 锁步组，冻结全部相关源码差异。

## 验证

- GEN_CCDB=0 ./scripts/compile.sh：CUDA+MPI 编译成功，日志 logs/compile/compile-20261009T200145-summary.log。
- 作业 610808，证据 runs/full_q_matrix_610808/：A-B、双 rank direct、单 rank direct、双 rank 全周期非均匀初值均完成 64 步并正常退出。
- 两组动态 AMR direct 均进入 level 2；各 1250 条阶段比较全部 linf=0；周期组 576 条全部 linf=0。
- 双 rank 与独立 A-B checkpoint 的全局 DDF linf=1.165734176e-15。
- valid/internal-ghost/interface 源诊断 mismatch 全为零。非周期 physical-ghost source mismatch 仍存在，不能声称所有 ghost 一致；周期组全部源类别 mismatch=0。
- 单 rank 采用进程内 A-B 锁步检查；不同 rank 数的 BoxArray 不保证一致，因此没有跨 rank 数 checkpoint 对照。第一次作业 610807 在这项比较前已通过单 rank 64 步阶段检查，随后因 BoxArray 不同触发比较器断言，保留原始日志。
- shell 语法、README 相对路径、CRLF-aware git diff --check 均通过。

## 证据边界

这是 64 步有限窗口验证，未扩展到长程、多节点或 GPU-aware device-direct MPI。原有用户循环换行修改保留在工作区，不纳入本提交。
