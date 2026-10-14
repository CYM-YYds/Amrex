# 新层只插值初始化 valid DDF

## 目的与改动

按用户明确的生命周期要求，BOX3D_OSI 的 InitializeDdfFromCoarse 只写入
fine BoxArray 的 valid 区，不扩大到 ghost，也不再裁剪 grown 工作区。
删除初始化末尾的 fine_bcs、GPU/CPU PhysBCFunct 和物理 ghost 延拓。

自定义插值公式、稀疏 coarse staging 及当前 coarse phase 的搬运保持不变。
coarse stencil 的物理域外延拓仍保留：fine valid 边缘插值也需要这些来源。
模板覆盖报错在工作区包含于 valid 时显示 fine_nghost=0。

fine ghost 由后续 Cycle2 的 FillGhostLevel、同层通信和 Boundary 维护，
初始化阶段不建立 ghost 有效性。此范围与既有 OSI MakeNewLevelFromScratch
只初始化 valid 的语义一致。本次未修改推进顺序或框架回调接口。

## 验证

- 生产 CUDA+MPI 构建通过。项目 compile.sh 的编译节点 SSH 连接被关闭，
  后续使用 scripts/lib/env.sh 的同一工具链及 make -j4 CUDA_ARCH=80 完成；
  日志：`../../runs/valid_init_20261014/production_build.log`。
- GPU 作业 **613389 / SUCCEEDED**：`../../runs/sparse_initialization_613389/`。
  1/2 rank、三种模式、全非周期/全周期/混合周期、coarse phase=0/17/41，
  共 **72 组**。fine valid 与独立 oracle 的最大差异 **2.220446049e-16**。
- 所有 fine 非 valid 存储（内部、周期和物理 ghost）预置 sentinel -777，
  初始化后逐 cell、逐 q 检查值未改变，全部 ghost_linf=0；检查也拒绝 NaN。
  测试的 coarse staging 为 648 个单元，原 grown 初始化为 1198–1536，
  parent valid 共 32768 个单元。这是该测试布局的暂存规模证据，不是速度测量。
- 父层模板缺口负例在插值前终止。
- 四组 64 步动态 AMR（mode 0 的 1/2 rank；mode 1/2 的 2 rank），
  第 32/64 步新增 level 1/2，覆盖 valid-only 构造后的实际首次推进。
  mode 0 对照修改前同 rank 的执行文件，**全 valid checkpoint linf=0**；
  mode 1/2 对照独立 canonical 运行，**active_linf=0**。
  **4464 条 osi_ab_stage 记录 linf=0**，包括重构后的全 valid 检查。
- mode 1/2 独立 canonical/OSI checkpoint 的 covered 粗层差异仍分别约
  0.00931062/0.009269；运行期非周期 physical-ghost source 的历史差异仍单独
  报告。不将 active 或初始化 valid 的结论扩展为全部 checkpoint covered/
  physical ghost source 相等，也不在初始化测试中要求 ghost 已被填成物理值。
- 测试入口沿用 `../../tests/build_sparse_initialization_test.sh` 和
  `../../tests/submit_sparse_initialization_validation.sh`，脚本语法检查及
  CRLF 感知 git diff --check 通过。
