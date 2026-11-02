#!/usr/bin/env bash
# 完整平均及 Remake 内部分项评估临时入口；任务后无复现依赖时可删除。
# 与缓存优化矩阵职责不同；复用现有 MPI 启动工具，不复制启动实现。
#DSUB --job_type cosched
#DSUB -n regrid_stage_evaluation
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=8;mem=32768;gpu=1
#DSUB -N 1
#DSUB -o runs/temporary/regrid_stage_evaluation/%J-submit.log
#DSUB -e runs/temporary/regrid_stage_evaluation/%J-submit.log
#DSUB -l cuda122
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
CASE="${REGRID_CASE_DIR:?提交前指定算例路径并冻结本脚本}"
REPO="$(git -C "$CASE" rev-parse --show-toplevel)"
TASK="$CASE/runs/temporary/regrid_stage_evaluation/round1"
OUT="$TASK/job${CCS_JOB_ID:?}"
mkdir "$OUT"
source "$CASE/scripts/osi_experiment_common.sh"
osi_write_hostfile "$OUT/hostfile"
cp "$TASK/reference.ex" "$TASK/candidate.ex" "$OUT/"
cp -a "$TASK/src" "$OUT/src"
cp "$TASK/config/inputs" "$OUT/inputs"
cp "$0" "$OUT/submit.sh"
sha256sum "$OUT"/*.ex > "$OUT/executable.sha256"
nvidia-smi > "$OUT/gpu.txt"
printf 'case\texit_code\n' > "$OUT/status.tsv"
common=(stop_time=100000 lbm.stream_mode=1 lbm.interp_mode=0
    amr.plot_int=-1 checkpoint.write_particles=false checkpoint.keep_latest_only=false
    verification.osi_ab_check=false verification.final_state_check=false
    verification.convergence_enabled=false verification.refinement_diagnostics=false
    performance.osi_detail_timing=0)
run() {
    local name=$1 app=$2
    shift 2
    mkdir "$OUT/$name"
    cd "$OUT/$name"
    osi_record_command command.txt "$OUT/$app.ex" "$OUT/inputs" "${common[@]}" "$@"
    local code=0
    osi_launch_mpi hmpi "$OUT/hostfile" 1 1 "$OUT/$app.ex" "$OUT/inputs" \
        "${common[@]}" "$@" > run.log 2>&1 || code=$?
    printf '%s\t%s\n' "$name" "$code" >> "$OUT/status.tsv"
    [[ "$code" == 0 ]]
}
small=(max_step=192 amr.n_cell=32 32 32 amr.max_level=2 amr.max_grid_size=16 16 16
    amr.blocking_factor_x=8 8 8 amr.blocking_factor_y=8 8 8 amr.blocking_factor_z=8 8 8
    amr.n_proper=2 amr.regrid_int=32 geometry.is_periodic=1 1 1
    verification.regrid_fixture=true verification.osi_seed_pattern=true
    checkpoint.begin_step=0 checkpoint.chk_int=192 performance.report_int=192)
run regression_baseline reference "${small[@]}"
run regression_detail candidate "${small[@]}" performance.regrid_detail_timing=true \
    verification.ddf_reference_checkpoint="$OUT/regression_baseline/chk00000192"
run regression_off candidate "${small[@]}" performance.regrid_detail_timing=false \
    verification.ddf_reference_checkpoint="$OUT/regression_baseline/chk00000192"
native=(amr.n_cell=128 128 128 amr.max_level=2 amr.max_grid_size=128 256 512
    amr.blocking_factor_x=16 16 16 amr.blocking_factor_y=16 16 16
    amr.blocking_factor_z=16 16 16 amr.n_proper=1 amr.regrid_int=32
    lbm.err=0.125 0.25 0.5 1 checkpoint.begin_step=128000
    checkpoint.chk_prefix="$CASE/runs/a5_B_optimized_128000/job622137/B/chk"
    checkpoint.chk_int=-1 max_step=128640 performance.report_int=160)
sequence=(false true true false false true true false)
for i in "${!sequence[@]}"; do
    run "timing_${i}_${sequence[$i]}" candidate "${native[@]}" \
        performance.regrid_detail_timing="${sequence[$i]}"
done
