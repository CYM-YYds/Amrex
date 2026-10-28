#!/usr/bin/env bash
# A5 对标前的阈值初测；本任务结束且不再需要复现时可删除，数据与报告保留。
#DSUB --job_type cosched
#DSUB -n a5_threshold_probe
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-a5-threshold.log
#DSUB -e logs/submit/%J-a5-threshold.log
#DSUB -l cuda122
set -euo pipefail
CASE=$(cd "$(dirname "$0")/../.." && pwd)
REPO=$(git -C "$CASE" rev-parse --show-toplevel)
OUT="$CASE/runs/temporary/a5_threshold_probe/job${CCS_JOB_ID:?}"
mkdir "$OUT"
RESTART_ROOT="${PROBE_RESTART_ROOT:-}"
END_STEP="${PROBE_END_STEP:-3200}"
[[ "$END_STEP" =~ ^[1-9][0-9]*$ ]] && (( END_STEP % 128 == 0 )) || {
    echo "PROBE_END_STEP 必须为正整数且为 128 的倍数" >&2; exit 1;
}
BEGIN_STEP=0
names=(A B C)
SNAPSHOT="$CASE"
APP="$CASE/main3d.gnu.FLOAT.TPROF.MPI.CUDA.ex"
RANK_SCRIPT="$CASE/scripts/run_rank.sh"
if [[ -n "$RESTART_ROOT" ]]; then
    RESTART_ROOT=$(realpath "$RESTART_ROOT")
    RESTART_CHECKPOINT="$RESTART_ROOT/A/chk00003200"
    [[ -f "$RESTART_CHECKPOINT/Header" ]] || { echo "缺少第一轮 A checkpoint" >&2; exit 1; }
    BEGIN_STEP=$(sed -n '3p' "$RESTART_CHECKPOINT/Header")
    [[ "$BEGIN_STEP" == 3200 ]] && (( END_STEP > BEGIN_STEP )) || exit 1
    # 续跑读取第一轮冻结程序与输入，不混入之后的工作树改动。
    names=(A)
    SNAPSHOT="$RESTART_ROOT"
    APP="$SNAPSHOT/application.ex"
    RANK_SCRIPT="$SNAPSHOT/A/run_rank.sh"
    cp "$SNAPSHOT/source.diff" "$OUT/application_source.diff"
    cp "$SNAPSHOT/git_head.txt" "$OUT/application_git_head.txt"
fi
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
if [[ -n "$RESTART_ROOT" ]]; then
    cp "$SNAPSHOT/inputs" "$OUT/inputs"
else
    cp "$SNAPSHOT/config/inputs" "$OUT/inputs"
fi
cp -a "$SNAPSHOT/src" "$OUT/src"
cp -a "$SNAPSHOT/config" "$OUT/config"
cp "$APP" "$OUT/application.ex"
cp "$0" "$OUT/submit.sh"
git -C "$CASE" rev-parse HEAD > "$OUT/git_head.txt"
git -C "$CASE" diff > "$OUT/source.diff"
sha256sum "$OUT/application.ex" "$OUT/inputs" > "$OUT/fingerprints.sha256"
nvidia-smi > "$OUT/gpu.txt"
awk 'NF >= 2 {print $1 " slots=" $2}' "${CCS_ALLOC_FILE:?}" > "$OUT/hostfile"
printf 'case\texit_code\n' > "$OUT/status.tsv"
printf 'restart_root=%s\nbegin_step=%s\nend_step=%s\n' "$RESTART_ROOT" "$BEGIN_STEP" "$END_STEP" > "$OUT/stage.txt"
common=(max_step="$END_STEP" stop_time="$((END_STEP / 128))"
    lbm.reynolds_number=1000 lbm.stream_mode=1 lbm.interp_mode=0
    amr.n_cell=128 128 128 amr.max_level=2 amr.regrid_int=32
    amr.max_grid_size=128 128 128
    amr.blocking_factor_x=16 16 16 amr.blocking_factor_y=16 16 16 amr.blocking_factor_z=16 16 16
    amr.n_error_buf=1 1 1 amr.grid_eff=0.7
    amr.plot_int=-1 checkpoint.begin_step="$BEGIN_STEP" checkpoint.chk_int="$END_STEP"
    checkpoint.chk_prefix=chk checkpoint.keep_latest_only=false
    verification.convergence_enabled=false verification.refinement_diagnostics=true
    verification.regrid_valid_check=true verification.final_state_check=true
    performance.report_int=320 performance.osi_detail_timing=0)
failed=0
for name in "${names[@]}"; do
    case "$name" in
        A) thresholds=(0.0625 0.125 0.25 0.5);;
        B) thresholds=(0.125 0.25 0.5 1);;
        C) thresholds=(0.25 0.5 1 2);;
    esac
    mkdir "$OUT/$name"
    cd "$OUT/$name"
    ln -s ../application.ex main3d.gnu.TPROF.MPI.CUDA.ex
    cp "$RANK_SCRIPT" run_rank.sh
    if [[ -n "$RESTART_ROOT" ]]; then
        ln -s "$RESTART_CHECKPOINT" chk00003200
    fi
    args=("${common[@]}" lbm.err="${thresholds[0]}" "${thresholds[@]:1}")
    printf '%q ' "$OUT/application.ex" "$OUT/inputs" "${args[@]}" > command.txt
    printf '\n' >> command.txt
    code=0
    "$REPO/.venv/bin/python" "$CASE/scripts/guard_run.py" --wall-seconds 1800 -- \
        mpirun -hostfile "$OUT/hostfile" -n 1 -npernode 1 -x PATH -x LD_LIBRARY_PATH \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
        env -u BASH_ENV bash --noprofile --norc -c \
        'export CUDA_VISIBLE_DEVICES=${OMPI_COMM_WORLD_LOCAL_RANK:-0}; exec bash ./run_rank.sh "$@"' \
        bash "$OUT/inputs" "${args[@]}" || code=$?
    printf '%s\t%s\n' "$name" "$code" >> "$OUT/status.tsv"
    echo "case=$name exit=$code output=$OUT/$name"
    if (( code != 0 )); then failed=1; fi
done
exit "$failed"
