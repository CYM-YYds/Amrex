# 完整平均下传统一修复物理边界

- 目的：避免调用方在 `AverageDownValid()` 后漏调当前态物理边界修复。
- 范围：仅 BOX3D_OSI 的完整平均入口、regrid 和宏观量调用链；逐层平均及边界核保持原实现。
- 改动：`AverageDownValid()` 在全部层平均完成后调用 `RepairCurrentStatePhysicalBoundary()`；单层同样修复。可选同步回调保留多层平均后、修复前的锁步检查点。移除两个调用处的独立修复调用，更新架构说明。
- 验证：`GEN_CCDB=0 ./scripts/compile.sh` CUDA+MPI 编译成功；job `610537` 使用既有 64 步锁步脚本正常结束。step 32 单层修复、step 64 多层平均及修复的 level-0 逐值检查均为 `unequal=0`、`nonfinite=0`、`linf=0`；重网格后 level 0/1/2 检查为有限且 `linf=0`。平均后检查仍早于修复后检查。
- 证据：`runs/20261009_111526_job610537/run.log`、`logs/submit/610537-osi-lockstep-alllevels-64.log`、`logs/compile/compile-20261009T111322-summary.log`。
- 边界：此次为调用链重构的单 GPU 回归，未重新验收多 GPU；Stream source 诊断仍有 physical ghost 差异，valid source 无差异，不据此声称所有 ghost 等价。
