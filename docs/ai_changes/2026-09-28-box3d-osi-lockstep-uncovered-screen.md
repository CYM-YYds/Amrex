# BOX3D_OSI 三层 uncovered 锁步筛查

- 目的：定位 OSI 与 A-B 分布函数首次出现差异的重网格区间。
- 文件：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/submit_osi_lockstep_alllevels_64.sh`。
- 主要设置：单 GPU、64^3 粗网格、`max_level=2`、每 32 步重网格、OSI/A-B 锁步检查。
- 比较范围：每层跳过被更细层覆盖的 cell；level 2 比较全部 valid cell；每个 cell 比较 27 个 DDF 分量。
- 验证：job `604714` 成功完成 64 步。step 32 前后各层 `linf=0`；step 64 的 `Stream` 阶段首次出现 finite mismatch：level 0 `linf=0.074389851228604503`，level 1 `linf=0.01846936708737125`，level 2 仍为 0。
- 证据：`projects/3Dcases/Computing_performance_test/BOX3D_OSI/logs/submit/604714-osi-lockstep-alllevels-64.log`。
