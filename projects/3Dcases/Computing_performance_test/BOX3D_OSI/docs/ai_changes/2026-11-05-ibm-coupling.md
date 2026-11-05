# BOX3D_OSI 三维静止 IBM 运行开关

目的与范围：在 BOX3D_OSI 实际 Cycle2/AdvanceLevel 中通过 inputs 控制 IBM，默认关闭。
保留本次开始时已有的后处理、图片、PPT 和 MLUPS 文档修改；没有继续改动 Cylinder3D。

## 实现

- RuntimeParams 是 IBM 配置唯一来源，启动时读取 enabled、物理 center/radius/length；
  几何类型复用已有 particles.shape，避免增加第二套 shape 参数。初始化/重启后配置不变。
- 原 OSI 和原通用 BGK 均将力设为零，不能通过取消 ComputeParticle 注释实现耦合。
  新增共享 Guo 受力 BGK 核，canonical/OSI 使用相同公式和原碰撞 mask/launch box；
  关闭 IBM 时保持原无力专用核及 overlap 分派。宏观速度按半步力修正，IBM 插值读取原始矩。
- 复用已有直接施力、SumBoundary、粒子存储和球面生成。球面生成改用容器实际半径，
  ConfigureIbm 同时维护重启校验所需 shape 与细层半径；新圆柱生成器负责 Y 轴侧壁、
  细层格子面积权重和轴向端点梯形积分。本算例没有圆柱生成器或 IDF，未建立跨算例依赖。
- 对物体及四点核支撑区补充细化标记，核支撑范围完整位于最细层；生成、重网格和读取后
  检查标记层级与流体/粒子 BA、DM 一致性。实际最细层步长传给 IBM，不再误用编译上限 rate。
- 粒子初始化和施力不再由 checkpoint.write_particles 决定。启用 IBM 的 checkpoint 总是保存
  标记及 IBM 配置，拒绝启用状态、几何或旧纯流体 checkpoint 的错误混用。
- AMReX Restart 在 dual-grid 读取后通过 SetParticleBoxArray/SetParticleDistributionMap
  将容器切换到私有 ParGDB 并恢复显式旧布局；这会阻止粒子继续跟随流体重网格。
  IBM 读取结束后用公开 Define(AmrCore::GetParGDB()) 重新绑定动态网格并 Redistribute，
  保留已读标记，让流体布局重新成为唯一来源。初轮 step17–23 受力相同，首次 regrid(step24)
  后错误旧 Fab 索引产生明显受力差异；初轮数据保留，不计作通过。

## 验证

构建：AMReX 26.06 / CUDA 12.8 / HMPI 1.2.0 / sm80，DIM=3，FLOAT 场和单精度粒子。
修改前 fd9b413 源码/可执行文件冻结为 baseline，构建入口确认当时程序 up-to-date。
新增 Guo 核的独立守恒矩与 A-B/OSI 相位地址检查，纳入既有 dimension_kernels_test，
DIM=2/3、FLOAT/DOUBLE 均在 CPU ASan/UBSan 下通过；该测试不声称二维已接入 IBM。

GPU 最终验收作业 626190，SUCCEEDED，22 项运行/拒绝检查通过。统一为 32³ coarse grid、
Re=100、32 个 coarse step、每 8 步 regrid、step16/32 checkpoint 和 step32 PlotFile；
主比较使用 max_level=1，另有 max_level=0 同网格开关对照与 max_level=2 有限值运行。

- 关闭 IBM 对修改前 baseline：所有层 valid DDF linf=0、unequal=0；旧纯流体 checkpoint
  重启以及关闭开关的连续/重启对照同样逐值相同。
- 球体/圆柱连续对 step16 重启：相同 valid topology，最大 DDF 差均为 8.940696716e-8。
- 圆柱独立 A-B 对 OSI：最大 DDF 差 8.940696716e-8；独立 OSI 对锁步运行 1.192092896e-7。
- 单 GPU 对双 GPU：最大 DDF 差 1.788139343e-7；双 GPU 默认 overlap+2MiB staging 对
  同程序关闭 overlap：1.192092896e-7。独立路径验收容差为 1e-6（MPI 为 2e-6），
  不将这些结果表述为逐位一致或把误差来源完全归因于舍入。
- 锁步 Collision/Communication/Stream/Boundary/Swap 阶段与 step8/16/24/32 重构后
  所有层 all-valid 检查通过；后者包含 covered/interface，日志中 linf=0。
  非周期 physical-ghost source mismatch 仍单列，不属于全部 ghost 等价验收。
- max_level=2 圆柱运行完成，所有层 valid DDF、速度和受力均有限；不声称三层严格 A-B 等价。
- 相同均匀网格下 IBM on/off 最大 DDF 差 5.343556404e-5，确认力项实际进入流体推进。
- 非法形状、碰撞模式、域原点、负尺寸、小于一个最细层 cell 的半径、checkpoint 启用状态和几何不匹配
  均按预期拒绝；IBM 开启且 checkpoint.write_particles=false 时仍保存标记。
- Shell bash -n、Python AST、文档链接/路径、AGENTS 软链检查通过。
  源文件保留既有行尾，空白检查采用 git -c core.whitespace=cr-at-eol diff --check。

完整逐值结果见 accepted/verification.json，各比较均针对 canonical 解码后的 valid DDF，
不含 ghost；独立比较同时要求 BoxArray/valid coverage 一致。

初次 CUDA 编译捕获了 device lambda 在 constexpr-if
内首次捕获变量的限制，已修正；最终日志为 runs/temporary/ibm_switch/build_reattach.log。

实验为临时验证，统一保留在 runs/temporary/ibm_switch/。顶层是初轮和诊断，
最终独立轮次在 accepted/；各轮保存输入、完整命令、冻结源码与差异、二进制 SHA256、
日志、checkpoint 和 PlotFile。临时入口 scripts/temporary/ibm_switch/submit.sh 与 verify.py
是报告的复现材料，保留；不覆盖生产输入的物理设置或迁移历史目录。

边界：保持原方腔初始/边界条件，圆柱仅侧壁，无端盖；限静止物体和单次直接施力。
不证明标准圆柱绕流、长时间稳定性、收敛、性能、多节点或运动 IBM 完整重启。
锁步检查使用同一次 OSI 宏观场得到的力供两布局碰撞，不能独立证明两套粒子插值相同；
独立 A-B/OSI 生产运行及 MPI 回归补充此边界。非周期 physical-ghost 的既有 source 差异
仍按阶段日志单列，不能据 valid 比较通过宣称全部 ghost 相同。
