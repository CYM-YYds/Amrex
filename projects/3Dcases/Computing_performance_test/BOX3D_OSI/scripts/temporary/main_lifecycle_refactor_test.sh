#!/usr/bin/env bash
# 本次 main 生命周期整理的短程回归；保留为测试结果的复现入口。
#DSUB --job_type cosched
#DSUB -n main_refactor
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=8;mem=16384;gpu=1
#DSUB -N 1
#DSUB -o runs/temporary/main_lifecycle_refactor/%J-submit.log
#DSUB -e runs/temporary/main_lifecycle_refactor/%J-submit.log
#DSUB -l cuda122
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
REPO_DIR="$(git -C "$CASE_DIR" rev-parse --show-toplevel)"
TASK_DIR="$CASE_DIR/runs/temporary/main_lifecycle_refactor"
source "$CASE_DIR/scripts/osi_experiment_common.sh"
osi_write_hostfile "$TASK_DIR/hostfile"
common=(max_step=8 amr.n_cell=32 32 32 amr.max_level=1
    amr.max_grid_size=32 32 amr.blocking_factor_x=8 8
    amr.blocking_factor_y=8 8 amr.blocking_factor_z=8 8
    amr.regrid_int=4 amr.plot_int=4 amr.begin_plot=0
    checkpoint.chk_int=4 checkpoint.keep_latest_only=false checkpoint.write_particles=false
    verification.convergence_enabled=false verification.final_state_check=true
    verification.regrid_fixture=true performance.report_int=4)
run() {
    local name=$1 executable=$2
    shift 2
    mkdir "$TASK_DIR/$name"
    cd "$TASK_DIR/$name"
    osi_record_command command.txt "$executable" "$TASK_DIR/inputs" "${common[@]}" "$@"
    osi_launch_mpi hmpi "$TASK_DIR/hostfile" 1 1 "$executable" "$TASK_DIR/inputs" \
        "${common[@]}" "$@" > run.log 2>&1
    echo "$name PASS"
}
if [[ "${MAIN_REFACTOR_SUITE:-all}" != amr ]]; then
run baseline "$TASK_DIR/baseline.ex"
run current "$TASK_DIR/current.ex" \
    verification.ddf_reference_checkpoint="$TASK_DIR/baseline/chk00000008"
run restart "$TASK_DIR/current.ex" checkpoint.begin_step=4 \
    checkpoint.chk_prefix="$TASK_DIR/current/chk" checkpoint.chk_int=-1 \
    verification.ddf_reference_checkpoint="$TASK_DIR/current/chk00000008"
# 单层锁步参考验证每步入口及推进钩子；主回归另行覆盖多层重构。
run hooks "$TASK_DIR/current.ex" amr.max_level=0 max_step=2 amr.plot_int=-1 \
    checkpoint.chk_int=-1 performance.report_int=1 verification.osi_ab_check=true \
    verification.osi_step_entry_check_interval=1 verification.osi_ab_continue_on_mismatch=false
fi
# 第 32 步实际创建细层，随后继续推进和输出；重启也跨越细层创建。
run baseline_amr "$TASK_DIR/baseline.ex" max_step=40 amr.plot_int=20 checkpoint.chk_int=20
run current_amr "$TASK_DIR/current.ex" max_step=40 amr.plot_int=20 checkpoint.chk_int=20 \
    verification.ddf_reference_checkpoint="$TASK_DIR/baseline_amr/chk00000040"
run restart_amr "$TASK_DIR/current.ex" max_step=40 amr.plot_int=20 checkpoint.chk_int=-1 \
    checkpoint.begin_step=20 checkpoint.chk_prefix="$TASK_DIR/current_amr/chk" \
    verification.ddf_reference_checkpoint="$TASK_DIR/current_amr/chk00000040"
"$REPO_DIR/.venv/bin/python" - "$TASK_DIR" <<'PY'
from pathlib import Path
import math
import re
import sys
root = Path(sys.argv[1])
names = ['baseline', 'current', 'restart', 'hooks', 'baseline_amr', 'current_amr', 'restart_amr']
logs = {name: (root / name / 'run.log').read_text() for name in names}
for name, log in logs.items():
    assert 'AMReX (26.06) finalized' in log, name
    assert '[FinalState]' in log, name
for name, levels in [('current', 1), ('restart', 1), ('current_amr', 2), ('restart_amr', 2)]:
    rows = re.findall(r'^ddf_cell_norm_level: (.+)$', logs[name], re.M)
    assert len(rows) == levels, (name, rows)
    for row in rows:
        values = dict(re.findall(r'(\w+)=([^ ]+)', row))
        for key in ['valid_linf', 'valid_l1', 'valid_l2', 'active_linf', 'active_l2']:
            assert math.isfinite(float(values[key])) and float(values[key]) == 0, (name, key)
    assert 'ddf_norm_end' in logs[name], name
for name in ['baseline_amr', 'current_amr', 'restart_amr']:
    assert 'AMR_LIFECYCLE MakeNewLevelFromCoarse level=1' in logs[name], name
for name, steps in [('baseline', [4, 8]), ('current', [4, 8]),
                    ('baseline_amr', [20, 40]), ('current_amr', [20, 40])]:
    for step in steps:
        assert (root / name / f'plt_{step:06d}' / 'Header').is_file()
        assert (root / name / f'chk{step:08d}' / 'Header').is_file()
for baseline, current in [('baseline', 'current'), ('baseline_amr', 'current_amr')]:
    a = re.findall(r'^step\d+ perf_count:.*$', logs[baseline], re.M)
    b = re.findall(r'^step\d+ perf_count:.*$', logs[current], re.M)
    assert a == b and a, (baseline, current)
assert 'stage=StepEntryUncovered' in logs['hooks']
assert 'step=2 stage=StepEntryUncovered' in logs['hooks']
assert not re.search(r'mismatches=[1-9]', logs['hooks'])
assert not re.search(r'finite=0', logs['hooks'])
report = '''# main 生命周期整理短测结果

- 三维 DOUBLE CUDA 编译通过。
- 7 组运行均完成并通过 FinalState 活跃单元有限值检查。
- 8 步回归、40 步实际细层创建回归及各自重启的每层 valid/active DDF 差异范数均为 0。
- 第 32 步创建 level 1；后续细层子步推进与输出正常。
- PlotFile/checkpoint Header 存在；重构前后性能工作计数一致。
- 单层锁步参考验证覆盖每步入口及推进，无 mismatch。
- 仅为单 GPU 小网格短测；未覆盖二维、FLOAT、多 rank、粒子和长期物理精度，未评价性能收益。

作业：621219（8 步与钩子）、621221（40 步）；621218 因测试脚本缺执行权限在启动前失败，修正权限后重交。
'''
(root / 'report.md').write_text(report)
print(report)
PY
