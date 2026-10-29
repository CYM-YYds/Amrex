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
OUTPUT_ROOT="${PROBE_OUTPUT_ROOT:-$CASE/runs/temporary/a5_threshold_probe}"
mkdir -p "$OUTPUT_ROOT"
OUT="$OUTPUT_ROOT/job${CCS_JOB_ID:?}"
mkdir "$OUT"
RESTART_ROOT="${PROBE_RESTART_ROOT:-}"
RESTART_CASE="${PROBE_RESTART_CASE:-A}"
SNAPSHOT_ROOT="${PROBE_SNAPSHOT_ROOT:-}"
PLOT_INT="${PROBE_PLOT_INT:--1}"
END_STEP="${PROBE_END_STEP:-3200}"
CHECKPOINT_INT="${PROBE_CHECKPOINT_INT:-$END_STEP}"
REPORT_INT="${PROBE_REPORT_INT:-320}"
CONVERGENCE="${PROBE_CONVERGENCE:-false}"
CONVERGENCE_STOP="${PROBE_CONVERGENCE_STOP:-true}"
REFINEMENT_DIAGNOSTICS="${PROBE_REFINEMENT_DIAGNOSTICS:-true}"
[[ "$END_STEP" =~ ^[1-9][0-9]*$ ]] && (( END_STEP % 128 == 0 )) || {
    echo "PROBE_END_STEP 必须为正整数且为 128 的倍数" >&2; exit 1;
}
BEGIN_STEP=0
read -r -a names <<< "${PROBE_CASES:-A B C}"
for name in "${names[@]}"; do
    case "$name" in A|B|C) ;; *) echo "PROBE_CASES 仅支持 A、B、C" >&2; exit 1;; esac
done
[[ "$PLOT_INT" =~ ^-?[0-9]+$ ]] || exit 1
if (( PLOT_INT > 0 )); then
    (( END_STEP % PLOT_INT == 0 )) || { echo "终点必须对齐输出间隔" >&2; exit 1; }
fi
SNAPSHOT="$CASE"
APP="$CASE/main3d.gnu.FLOAT.TPROF.MPI.CUDA.ex"
RANK_SCRIPT="$CASE/scripts/run_rank.sh"
if [[ -n "$RESTART_ROOT" ]]; then
    case "$RESTART_CASE" in A|B|C) ;; *) echo "PROBE_RESTART_CASE 必须为 A、B 或 C" >&2; exit 1;; esac
    RESTART_ROOT=$(realpath "$RESTART_ROOT")
    RESTART_CHECKPOINT="$RESTART_ROOT/$RESTART_CASE/chk00003200"
    [[ -f "$RESTART_CHECKPOINT/Header" ]] || { echo "缺少第一轮 $RESTART_CASE checkpoint" >&2; exit 1; }
    BEGIN_STEP=$(sed -n '3p' "$RESTART_CHECKPOINT/Header")
    [[ "$BEGIN_STEP" == 3200 ]] && (( END_STEP > BEGIN_STEP )) || exit 1
    # 续跑读取第一轮冻结程序与输入，不混入之后的工作树改动。
    names=("$RESTART_CASE")
    SNAPSHOT="$RESTART_ROOT"
    APP="$SNAPSHOT/application.ex"
    RANK_SCRIPT="$SNAPSHOT/$RESTART_CASE/run_rank.sh"
elif [[ -n "$SNAPSHOT_ROOT" ]]; then
    # 从静止重跑也能沿用先前冻结的程序与配置。
    SNAPSHOT=$(realpath "$SNAPSHOT_ROOT")
    APP="$SNAPSHOT/application.ex"
    RANK_SCRIPT="$SNAPSHOT/${names[0]}/run_rank.sh"
fi
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
if [[ -n "$RESTART_ROOT" || -n "$SNAPSHOT_ROOT" ]]; then
    cp "$SNAPSHOT/inputs" "$OUT/inputs"
    cp "$SNAPSHOT/source.diff" "$OUT/application_source.diff"
    cp "$SNAPSHOT/git_head.txt" "$OUT/application_git_head.txt"
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
export MPLCONFIGDIR="$OUT/mplconfig"
mkdir -p "$MPLCONFIGDIR"
printf 'restart_root=%s\nsnapshot=%s\ncases=%s\nbegin_step=%s\nend_step=%s\nplot_int=%s\n' "$RESTART_ROOT" "$SNAPSHOT" "${names[*]}" "$BEGIN_STEP" "$END_STEP" "$PLOT_INT" > "$OUT/stage.txt"
common=(max_step="$END_STEP" stop_time="$((END_STEP / 128))"
    lbm.reynolds_number=1000 lbm.stream_mode=1 lbm.interp_mode=0
    amr.n_cell=128 128 128 amr.max_level=2 amr.regrid_int=32
    amr.blocking_factor_x=16 16 16 amr.blocking_factor_y=16 16 16 amr.blocking_factor_z=16 16 16
    amr.n_error_buf=1 1 1 amr.grid_eff=0.7
    amr.plot_file=plt_ amr.plot_int="$PLOT_INT" amr.begin_plot=0
    checkpoint.begin_step="$BEGIN_STEP" checkpoint.chk_int="$CHECKPOINT_INT"
    checkpoint.chk_prefix=chk checkpoint.keep_latest_only=false
    verification.convergence_enabled="$CONVERGENCE" verification.convergence_stop="$CONVERGENCE_STOP"
    verification.refinement_diagnostics="$REFINEMENT_DIAGNOSTICS"
    verification.regrid_valid_check=true verification.final_state_check=true
    performance.report_int="$REPORT_INT" performance.osi_detail_timing=0)
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
    # 复用公共提交入口的覆盖变量，供短回归与正式运行记录额外参数。
    read -r -a extra_args <<< "${AMREX_RUN_ARGS:-}"
    args+=("${extra_args[@]}")
    ln -s ../inputs inputs
    printf 'case=%s\nprecision=FLOAT\nexecutable=%s\n' "$name" "$OUT/application.ex" > manifest.txt
    cat "$OUT/fingerprints.sha256" >> manifest.txt
    printf '%s ' "${args[@]}" > overrides.txt
    printf '\n' >> overrides.txt
    printf '%q ' "$OUT/application.ex" "$OUT/inputs" "${args[@]}" > command.txt
    printf '\n' >> command.txt
    code=0
    "$REPO/.venv/bin/python" "$CASE/scripts/guard_run.py" --wall-seconds "${PROBE_WALL_SECONDS:-1800}" -- \
        mpirun -hostfile "$OUT/hostfile" -n 1 -npernode 1 -x PATH -x LD_LIBRARY_PATH \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
        env -u BASH_ENV bash --noprofile --norc -c \
        'export CUDA_VISIBLE_DEVICES=${OMPI_COMM_WORLD_LOCAL_RANK:-0}; exec bash ./run_rank.sh "$@"' \
        bash "$OUT/inputs" "${args[@]}" || code=$?
    printf '%s\t%s\n' "$name" "$code" >> "$OUT/status.tsv"
    echo "case=$name exit=$code output=$OUT/$name"
    if (( code == 0 )) && [[ "$CONVERGENCE" == true ]]; then
        "$REPO/.venv/bin/python" "$CASE/data_post_processing/plot_convergence.py" \
            --log run.log --output convergence.png --title "$name Re=1000 convergence"
    fi
    if (( code == 0 )) && [[ "${PROBE_PROFILE:-false}" == true ]]; then
        # 数据提取和误差绘图继续使用现有入口；失败保留日志，不删除场数据。
        post_code=0
        "$REPO/.venv/bin/python" "$CASE/data_post_processing/extract_run_centerline.py" \
            "$OUT/$name" --backend batch --output-dir "$OUT/$name/profile" > profile.log 2>&1 || post_code=$?
        if (( post_code == 0 )); then
            "$REPO/.venv/bin/python" "$CASE/data_post_processing/plot_centerline.py" \
                --profile "$OUT/$name/profile/centerline.csv" "$name" "$END_STEP" \
                --height 1 --lid-velocity 0.05 --origin 0 \
                --reference "${PROBE_REFERENCE:?}" --reference-source 'Jaber Fig.18(a), digitized AMR and Cortes-Miller points' \
                --title "$name Re=1000 step $END_STEP vs Fig.18(a)" \
                --output "$OUT/$name/profile/reference_comparison.png" >> profile.log 2>&1 || post_code=$?
        fi
        printf '%s\n' "$post_code" > profile_exit_code.txt
    fi
    if (( code != 0 )); then failed=1; fi
done
exit "$failed"
