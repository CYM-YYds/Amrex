# 新层初始化按实际粗层插值模板暂存

## 目的与范围

仅修改 BOX3D_OSI 新层初始化及其与 FillGhostLevel 共用的局部机制。
原初始化对整个父层分配、复制/解码、计算宏观量并缩放，再由
InterpFromCoarseLevel 收集一次模板。改为按目标 fine 区域直接收集所需
coarse stencil，减少无关父层数据的分配、处理和中转。

## 主要改动

- fine 工作区保持原初始化语义：valid 加请求 ghost；非周期方向裁剪到物理域，
  周期方向保留周期 ghost。用所选插值器的 BoxCoarsener 构造完整模板。
- 与 ghost 路径共用 BuildInterpolationStage、父层 valid/周期像覆盖检查、
  非周期最近单元延拓及 ScaleInterpolationStage。每个模板的 owner 与对应
  fine Fab 相同；模板允许重叠，未增加全局去重状态。
- canonical 来源使用 ParallelCopy；OSI 来源使用既有 ParallelCopyOsi，
  按当前 coarse phase 直接写入零 ghost 的稀疏 canonical staging。
  初始化计划只在调用期间存在，不进入长期 ghost CPC 缓存。
- 删除初始化的全父层 ComputeMacroLevel、全层 canonical 副本及无调用者的
  ScaleCanonical/InterpolateCanonicalToFine；A-B 新层参考态也按同一范围
  从独立 A-B DDF 收集模板。
- 初始化继续使用原 AMReX 插值器，通过 FillPatchInterp 消费已准备的 staging，
  不再二次收集模板。其限幅规则与运行期 ghost 自定义 kernel 不完全相同，
  因此没有把初始化插值公式换成 ghost kernel。
- 初始化使用临时 BCRec，将周期方向标为 int_dir，非周期方向保留 foextrap。
  首轮混合周期测试在旧、新路径的 ghost 中均发现 NaN，valid 未发现 NaN；
  此修正避免物理面填充对周期方向作错误延拓，范围限定在该初始化入口。
- 模板内求宏观速度使用 average_scale 的直接除法，旧 OSI 宏观量使用倒数乘法，
  因而验证按浮点舍入量级比较，不宣称逐 bit 等价。

## 验证

- 生产 CUDA+MPI 构建通过。首次使用 scripts/compile.sh（GEN_CCDB=0、MAKE_J=8）；
  后续编译节点 SSH 连接被关闭，最终增量构建使用同一共享 MPI/CUDA/GCC 工具链及
  scripts/lib/ccdb.sh 的 make 命令（make -j4 CUDA_ARCH=80），退出码为 0。
  最终日志：`../../runs/sparse_init_20261013/production_final_build.log`。
- GPU 作业 **613335 / SUCCEEDED**：`../../runs/sparse_initialization_613335/`。
  目录冻结 before/after/test 执行文件、SHA256、输入、命令及被测函数源码。
  生产 after 执行文件 SHA256 为
  `9a771bae4c8650e9059d3996af31d69a3c752f2438b6c24bc0b664eca91f6345`。
- 独立测试直接提取生产函数，使用非均匀 DDF、错开的 coarse/fine owner，
  对照全父层缩放加 AMReX 插值的旧流程。覆盖 1/2 rank、interp_mode=0/1/2、
  全非周期/全周期/混合周期及 coarse phase=0/17/41，共 **72 组**。
  fine valid 与全部两层 ghost 均有限，逐值最大差异 **2.775557562e-17**。
  混合周期参考端采用正确的 int_dir/foextrap 记录；被测入口故意接收驱动的
  全 foextrap 记录，以验证入口对周期方向的修正。
- 测试布局中，父层 valid 有 32768 个单元，稀疏模板仅 1198–1536 个单元，
  staging ghost 为 0。此结果证明减少该布局的暂存规模，不代表所有布局的比例
  或已测量的总体加速；密集 fine 覆盖及邻接模板重叠可能降低收益。
- 父层 valid 模板缺口负例在插值前终止，并输出 InterpolationCoverage、
  fine_level 和 missing_coarse_boxes。
- 四组动态 AMR（mode 0 的 1/2 rank；mode 1/2 的 2 rank），新旧各运行 64 步，
  regrid_int=32。实际在第 32/64 步新增 level 1/2，覆盖新层构造入口。
  同 rank、同布局 checkpoint 的 12 个 level 全 valid DDF 最大差异
  **1.332267630e-15**；修改后 **4464 条 osi_ab_stage 记录 linf=0**。
  source 检查中的 valid/internal ghost/interface/covered mismatch 均为 0。
- 动态回归的非周期 physical-ghost source mismatch 在旧、新执行文件中仍存在，
  不将上述 valid 或阶段结果解释为所有物理 ghost source 完全相等。
  混合周期初始化 ghost 的独立逐值验证与该历史 source 诊断分别报告。
- 两个新增 shell 脚本通过 bash -n；CRLF 感知的 git diff --check 通过。

## 复现入口

在独立构建目录设置 SPARSE_INIT_BUILD_DIR，加载目标工具链后运行
`../../tests/build_sparse_initialization_test.sh`。测试构建配置为
`../../tests/SparseInitialization.GNUmakefile`。

提交 `../../tests/submit_sparse_initialization_validation.sh` 时指定
SPARSE_INIT_TEST_EXE（独立测试执行文件）及 SPARSE_INIT_BASELINE_EXE（修改前生产
执行文件）；脚本按相同输入和 rank 数对照新旧版本，运行目录独立保留证据。
