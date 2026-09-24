# BOX3D_OSI 第 32 步入口逐点核对

- 目的：确认初次重网格前现有单层 uncovered DDF 是否与 A-B oracle 一致，并核对旧阶段范数与全 valid 范数的矛盾。
- 修改文件：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/src/main.cpp`、`src/AmrCoreLBM.H/.cpp`、`README.md`、`docs/current_status.md`；本记录。未修改用户现有配置与其他源码改动。
- 主要改动：新增 `verification.osi_step_entry_check_step`，在指定步的入口、边界修复后与重网格后，把 A-B/OSI 状态拷回主机逐 cell、逐 q 比较 level 0 全 valid，并分别统计 uncovered/covered；删除已证实误导的自动 A-B 批次范数输出。
- 验证：CUDA+MPI 编译通过。比较器的速度表与 `D3Q19.H` 27 个方向逐项一致。job `603306` 的 step 32 入口 56,623,104 个 valid DDF 全相等；job `603307` 的入口、修复后和重网格后三次比较均 `unequal=0`、`nonfinite=0`，重网格后 uncovered/covered 也全相等。最终编译版本 job `603308` 的 step 33 入口 uncovered 49,545,216 个值仍相等，covered 有 6,134,587 个值不同，随后旧阶段检查在 `Stream` 报告 uncovered 差异。jobs `603298`/`603305` 用过错误的手写速度表，其差异数字无效；旧批次范数 `0.07579002442` 与可靠逐点结果冲突。作业日志和运行快照均保留。
