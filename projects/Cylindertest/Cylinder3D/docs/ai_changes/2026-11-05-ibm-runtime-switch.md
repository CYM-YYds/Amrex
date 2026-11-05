# Cylinder3D：IBM 运行开关与几何/施力方法选择

目的：通过 inputs 控制 IBM 是否参与，复用已有球体/圆柱初始化与 IDF/MDF 实现，
消除使用者必须修改注释才能切换的操作。范围限于 Cylinder3D；未修改 BOX3D_OSI。

主要改动：

- RuntimeParams 统一读取 `ibm.enabled/geometry/method`，默认 true/sphere/idf，启动打印并校验。
  参数仅在启动时设置，运行期间不变，无额外编译开关或缓存配置。
- 关闭 IBM 时在既有高层入口跳过粒子、施力、迁移、诊断与粒子 checkpoint；保留零力数组供
  原碰撞核使用，新增细层时也明确归零。取消物体加密，递归推进改用实际最细层。
- 圆柱复用 Y 轴侧壁生成器，修正轴向坐标/高度的粗层与细层单位混用，面积权重转换为施力核
  所需的细层格子面积；加密距离使用 XZ 径向距离，并覆盖完整轴向范围。
- MDF 单次迭代核原先按粗层 NX/NY/NZ 回绕细层索引，导致圆柱首次施力即产生 NaN。
  移除不适用于当前非周期域的回绕，保持当前层全局索引并读取既有 FAB ghost。
- checkpoint 新增 IBMConfig 元数据；拒绝开关不一致、启用时几何/方法不一致或未存粒子的重启。
  无元数据的旧 checkpoint 按原 sphere/idf 路径兼容。
- 算例提交入口资源账户更新为现场验证使用的 root.iosoeqkp，并默认选择本算例不带 TPROF 的
  可执行文件名；保留 AMREX_EXECUTABLE 覆盖能力。使用现有公共编译/提交机制，无新增长期入口。

验证与保留范围：

- CUDA + MPI、AMReX 26.06 编译。首次遇到迁移前 AMReX 26.01 对象文件的链接失败，
  保留旧对象后重新编译本算例源文件；未运行 clean.sh。最终构建日志为
  `logs/compile/compile-20261105T114312.log`。
- 以修改前提交 c21a176 的原始源码独立构建 baseline，共用同一 AMReX 26.06 工具链。
  所有运行冻结可执行文件与 inputs，单 GPU / 单 MPI rank，Re=100，max_step=64，
  regrid_int=10，step32/64 checkpoint，step64 速度 PlotFile。
- 最终作业：matrix 626182、参数拒绝与旧格式重启 626183，均 SUCCEEDED；baseline 作业 626178。
- 开关关闭、球体 IDF/MDF、圆柱 MDF 均完成 64 步，所有层 valid f_old/f_new 与速度场均有限；
  启用时受力记录有限且非零，关闭时没有受力文件或粒子 checkpoint。
- step64 默认球体 IDF 对原源码 baseline、隐式默认对显式参数的 valid f_old/f_new
  最大差为 4.440892098500626e-16，分别有 59117/53933 个值不逐位一致，通过 1e-12 容差。
  不能将此次默认兼容回归描述为 bitwise 相同。
- IBM off/球体 IDF/圆柱 MDF 的连续运行对 step32 重启，以及无 IBMConfig 的旧 checkpoint
  重启，valid f_old/f_new 均逐值一致：linf=0、unequal=0。全部比较不覆盖 ghost 区域。
- 非法 geometry/method 以及 off checkpoint 以 enabled=true 重启，均按预期报错。
- Shell bash -n、Python AST、文档链接/路径、AGENTS 软链检查通过；保留源码既有 CRLF，
  使用 git -c core.whitespace=cr-at-eol diff --check 检查空白。

输入、命令、二进制 SHA256、源码 diff、构建/运行日志、checkpoint、PlotFile 与 JSON 报告保留在
`runs/temporary/ibm_runtime_switch/`。此前失败与中间轮次分别保留在
`failed_coarse_index_wrap/`、`before_area_unit_fix/`，不得作为最终验收结果引用。
临时复现入口 `scripts/temporary/ibm_runtime_switch/submit.sh` 支持
`IBM_VALIDATION_STAGE=baseline/matrix/negative`（通过 dsub -x 传入）；
从仓库根执行 `.venv/bin/python projects/Cylindertest/Cylinder3D/scripts/temporary/ibm_runtime_switch/verify.py`。
这些脚本是检查报告的复现材料，保留。该实验为临时验证，不迁移或清理历史结果。

边界：短程有限值和回归不证明物理收敛、圆柱端部条件、长时间稳定性、多 GPU 正确性或性能。
圆柱仅有侧壁，无端盖；外边界不自动改变。CdCl 文件仍按球体投影面积归一化，压力采样仍是
原球体实现，不能直接作为标准圆柱系数与压力结果。当前 MDF 验证 NF=1；圆柱 IDF 与 NF>1 未验收。
