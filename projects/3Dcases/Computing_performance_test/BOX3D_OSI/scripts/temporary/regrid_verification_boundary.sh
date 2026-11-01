#!/usr/bin/env bash
# 本次重构检查职责调整的临时回归入口；结果留存期间用于复现，之后可删除。
#DSUB --job_type cosched
#DSUB -n regrid_check_boundary
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=8;mem=16384;gpu=2
#DSUB -N 1
#DSUB -o runs/temporary/regrid_verification_boundary/%J-submit.log
#DSUB -e runs/temporary/regrid_verification_boundary/%J-submit.log
#DSUB -l cuda122
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
REPO_DIR="$(git -C "$CASE_DIR" rev-parse --show-toplevel)"
TASK_DIR="$CASE_DIR/runs/temporary/regrid_verification_boundary/round1"
OUT="$TASK_DIR/job${CCS_JOB_ID:?}"
mkdir "$OUT"
source "$CASE_DIR/scripts/osi_experiment_common.sh"
osi_write_hostfile "$OUT/hostfile"
cp "$0" "$OUT/submit.sh"
cp "$TASK_DIR/inputs" "$OUT/inputs"
cp "$TASK_DIR/baseline.ex" "$TASK_DIR/candidate.ex" "$OUT/"
cp -a "$TASK_DIR/candidate_src" "$OUT/src"
sha256sum "$OUT/baseline.ex" "$OUT/candidate.ex" > "$OUT/executable.sha256"
nvidia-smi > "$OUT/gpu.txt"
printf 'case\texit_code\n' > "$OUT/status.tsv"
common=(max_step=192 stop_time=1000 amr.n_cell=32 32 32 amr.max_level=1
    amr.max_grid_size=16 16 amr.blocking_factor_x=8 8 amr.blocking_factor_y=8 8
    amr.blocking_factor_z=8 8 amr.n_proper=2 amr.regrid_int=32 amr.plot_int=-1
    checkpoint.begin_step=0 checkpoint.chk_int=32 checkpoint.keep_latest_only=false
    checkpoint.write_particles=false verification.convergence_enabled=false
    verification.final_state_check=false verification.regrid_fixture=true
    verification.osi_ab_check=false verification.osi_ab_continue_on_mismatch=false
    verification.osi_step_entry_check_interval=32
    performance.report_int=32 performance.osi_detail_timing=0 lbm.stream_mode=1
    lbm.osi_mpi_device_direct=0 lbm.osi_mpi_pipeline_chunk_bytes=0)
run() {
    local name=$1 ranks=$2 executable=$3
    shift 3
    mkdir "$OUT/$name"
    cd "$OUT/$name"
    osi_record_command command.txt "$OUT/$executable.ex" "$OUT/inputs" "${common[@]}" "$@"
    local code=0
    osi_launch_mpi hmpi "$OUT/hostfile" "$ranks" "$ranks" "$OUT/$executable.ex" \
        "$OUT/inputs" "${common[@]}" "$@" > run.log 2>&1 || code=$?
    printf '%s\t%s\n' "$name" "$code" >> "$OUT/status.tsv"
    [[ "$code" == 0 ]]
}
for ranks in 1 2; do
    domain=(0 0 0)
    seed=false
    if [[ "$ranks" == 2 ]]; then domain=(1 1 1); seed=true; fi
    run "baseline_r$ranks" "$ranks" baseline geometry.is_periodic="${domain[@]}" \
        verification.osi_seed_pattern="$seed" verification.regrid_valid_check=true
    run "production_r$ranks" "$ranks" candidate geometry.is_periodic="${domain[@]}" \
        verification.osi_seed_pattern="$seed" \
        verification.ddf_reference_checkpoint="$OUT/baseline_r$ranks/chk00000192"
done
# 旧命令行残留不能绕过生产模式；该参数已退出运行时配置。
run legacy_production 1 candidate verification.regrid_valid_check=true \
    verification.osi_seed_pattern=false geometry.is_periodic=0 0 0 \
    verification.ddf_reference_checkpoint="$OUT/baseline_r1/chk00000192"
checks=(max_step=64 checkpoint.chk_int=-1 verification.osi_ab_check=true
    verification.osi_seed_pattern=true geometry.is_periodic=1 1 1)
for ranks in 1 2; do
    run "interval_r$ranks" "$ranks" candidate "${checks[@]}"
done
run target_step 1 candidate "${checks[@]}" verification.osi_step_entry_check_interval=-1 \
    verification.osi_step_entry_check_step=64
run no_target 1 candidate "${checks[@]}" verification.osi_step_entry_check_interval=-1
"$REPO_DIR/.venv/bin/python" - "$OUT" <<'PY'
from pathlib import Path
import csv
import hashlib
import math
import re
import sys

root = Path(sys.argv[1])
rows = list(csv.DictReader((root / 'status.tsv').open(), delimiter='\t'))
assert len(rows) == 9, rows
logs = {}
for row in rows:
    name = row['case']
    log = (root / name / 'run.log').read_text()
    logs[name] = log
    assert row['exit_code'] == '0', row
    assert 'AMReX (26.06) finalized' in log, name
    # 终点诊断会完整平均 covered 粗单元；关闭它，按检查点保存阶段比较。
    assert '[FinalState]' not in log, name
    assert not re.search(r'\bfinite=0\b|osi_ab_stage_failure', log), name
    assert not re.search(r'\bmismatches=[1-9]', log), name

for name in ['production_r1', 'production_r2', 'legacy_production']:
    log = logs[name]
    assert not re.search(r'^regrid_valid_check |^osi_ab_', log, re.M), name
    assert 'AMR_LIFECYCLE RemakeLevel' in log, name
    assert 'AMR_LIFECYCLE ClearLevel' in log, name
    norms = re.findall(r'^ddf_cell_norm_level: (.*)$', log, re.M)
    assert len(norms) == 2, (name, norms)
    for record in norms:
        values = dict(re.findall(r'(\w+)=([^ ]+)', record))
        for key in ['valid_linf', 'valid_l1', 'valid_l2', 'active_linf', 'active_l2']:
            value = float(values[key])
            assert math.isfinite(value) and value == 0, (name, key, value)
    reference = 'baseline_r2' if name.endswith('r2') else 'baseline_r1'
    for step in [32, 64, 96, 128, 160, 192]:
        before = root / reference / f'chk{step:08d}'
        after = root / name / f'chk{step:08d}'
        source = sorted(before.glob('Level_*/f_old_D_*'))
        assert source, (name, step)
        assert [p.relative_to(before) for p in source] == [p.relative_to(after) for p in sorted(after.glob('Level_*/f_old_D_*'))], (name, step)
        for original in source:
            target = after / original.relative_to(before)
            assert hashlib.sha256(original.read_bytes()).digest() == hashlib.sha256(target.read_bytes()).digest(), target
    assert 'regrid_valid_check level=' in logs[reference], reference

for name, expected in [('interval_r1', [32, 64]), ('interval_r2', [32, 64]),
                       ('target_step', [64]), ('no_target', [])]:
    log = logs[name]
    steps = [int(s) for s in re.findall(
        r'^osi_ab_all_valid_begin: step=(\d+) stage=AfterRefineMeshAllValid', log, re.M)]
    assert steps == expected, (name, steps)
    finite = re.findall(r'^osi_ab_all_valid_finite:.*stage=AfterRefineMeshAllValid.*finite=1$', log, re.M)
    assert len(finite) == 2 * len(expected), (name, finite)
    assert 'regrid_valid_check level=' not in log, name

report = '''# 重构检查职责回归

- 9 组运行正常退出，覆盖三维 FLOAT、单/双 rank、六面非周期/全周期。
- 生产模式即使残留旧独立参数，也没有重构全 valid 检查或 OSI/A-B 检查输出。
- 生产回归覆盖细层创建、布局重构、清除、再次创建；逐层 valid/active DDF 差异范数均为 0，六个检查点的 DDF 数据文件逐字节一致。
- 锁步模式按 32 步间隔或指定第 64 步执行全 valid 有限值检查与比较；未选择目标步时不执行。
- 不涉及性能测量、二维、DOUBLE、device-direct MPI 或长期物理精度验收。
'''
(root / 'report.md').write_text(report)
print(report)
PY
