# 固定单层同步与无mask碰撞接入

## 目的与范围

将[独立实验](2026-10-29-uniform-sync-mask-tests.md)中的两项候选接入现有求解器，
仅配置 `amr.max_level=0` 时启用；默认AMR配置不选择这两项优化。
用户已有inputs、三维AMR判据、MLUPS文档和后处理脚本修改不属于本次提交。

## 代码与职责

- `AmrCoreLBM.H/.cpp`：RuntimeParams新增两个默认true的配置值，分别由
  `performance.uniform_reduce_sync`、`lbm.uniform_nomask_collision` 在ReadParameters读取一次。
  `useUniformReducedSync()`、`useUniformNoMaskCollision()`集中派生实际策略，不新增缓存。
  策略依据配置最高层，不能因当前没有细层而绕过AMR。
- `AmrCoreLBM_advance.cpp`：Collide在host选择 `CollideLayoutLevel<false/true>`。
  模板内部在host编译期选择无mask/带mask的GPU闭包；各闭包继续调用已有碰撞公式函数。
  独立启动闭包用于真正删除mask参数捕获，未复制公式或新增流场数组。
  public模板辅助函数符合本工程CUDA扩展设备lambda的访问要求。
- 减少同步时，关闭comm/stream/boundary/swap主阶段计时，并去掉MFIter析构后的重复等待；
  在AdvanceOneLayout末尾显式等待GPU，保证外层solv/total计时和下一步依赖。
  单层空平均修复的boundary计时也按该策略关闭，避免未测量字段混入空阶段耗时。
- `AmrCoreLBM_osi.cpp`：单rank本地复制在同一stream有序执行，采用步末等待；
  MPI pack、D2H/H2D、跨stream和缓冲区生命周期等待保留。
- `main.cpp`：perf(s)输出两个实际生效开关。减少同步时相关阶段为未测量，不能据0值判断无成本。

AMR使用原mask及阶段同步；开启OSI细分计时或锁步参考时自动保留原阶段同步。
无mask碰撞开关独立于同步开关，二者都可设为0做回退对照。
初始化、checkpoint格式、输出顺序及收敛判据沿用现有生命周期。

最初在设备lambda内使用if constexpr触发NVCC首次捕获变量限制；直接使用模板常量条件
虽能消除分支，仍保留掩码捕获参数。因此最终将编译期分派移至GPU启动外层。
早期兼容版本的编译及回归材料保留在round1，最终源码及验收以round2为准。

## 验证与复现

正式接入回归目录：[runs/temporary/uniform_integration/round2](../../runs/temporary/uniform_integration/round2)。
这是临时短程回归；保留接入前后源码、程序、哈希、输入、命令、编译和运行日志、
checkpoint及结果。round1保留首轮CUDA兼容调整证据，不计为最终源码验收。

复用 [uniform_dispatch.sh](../../scripts/temporary/uniform_dispatch.sh)，仅增加实验目录、
正确性网格大小及只运行数值回归的参数，不新增提交入口。
最终三维FLOAT组合路径对照接入前基线：64³、单/双rank、周期/非周期、A-B/OSI，
另含restart、锁步及40步实际创建细层的AMR回归。
两端AMR均在第40步场输出完成完整平均后再比较，避免covered粗层取样阶段不一致。

本次检验生产接入后的组合路径，不重放性能矩阵，也不将两项独立收益直接相加。
二维共用代码只做FLOAT CUDA编译检查，二维数值回归、DOUBLE及长程物理精度不在本轮范围。

最终作业621579完成21组运行，10次checkpoint逐值对照的各层valid/active
L1/L2/Linf均有限且为0；restart、16步双rank周期锁步及第32步创建细层的AMR回归通过。
日志确认普通单层实际开关为1/1、AMR为0/0；锁步按策略保留阶段同步，阶段逐值检查通过。
双rank单层布局覆盖发送源和剩余块并存的碰撞通信重叠路径。
二维/三维FLOAT CUDA编译、Shell语法和文档链接检查通过；保留原CRLF，
diff检查使用 `git -c core.whitespace=cr-at-eol diff --check`。

最终无mask OSI入口为168个寄存器、536 bytes cmem[0]，恢复了独立候选去掉掩码参数的
编译特征。组合回归不等于组合性能验证，本次未新增组合加速比。
完整运行与退出证据位于
[job621579](../../runs/temporary/uniform_integration/round2/job621579)，
关键汇总为 [checks.json](../../runs/temporary/uniform_integration/round2/job621579/checks.json)。
