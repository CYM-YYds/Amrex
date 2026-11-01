#!/usr/bin/env bash
# 本次缓存复用及速度补边优化的临时验证入口；作为结果复现材料保留，之后可删除。
#DSUB --job_type cosched
#DSUB -n regrid_cache_reuse
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=8;mem=32768;gpu=2
#DSUB -N 1
#DSUB -o runs/temporary/regrid_cache_reuse/%J-submit.log
#DSUB -e runs/temporary/regrid_cache_reuse/%J-submit.log
#DSUB -l cuda122
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
CASE="${REGRID_CASE_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
REPO="$(git -C "$CASE" rev-parse --show-toplevel)"
TASK="$CASE/runs/temporary/regrid_cache_reuse/round1"
OUT="$TASK/job${CCS_JOB_ID:?}"
mkdir "$OUT"
source "$CASE/scripts/osi_experiment_common.sh"
osi_write_hostfile "$OUT/hostfile"
cp "$TASK/baseline.ex" "$TASK/candidate.ex" "$OUT/"
cp -a "$TASK/candidate_src" "$OUT/src"
cp "$TASK/config/inputs" "$OUT/inputs"
cp "$0" "$CASE/scripts/temporary/analyze_regrid_cache_reuse.py" "$OUT/"
sha256sum "$OUT"/*.ex > "$OUT/executable.sha256"
nvidia-smi > "$OUT/gpu.txt"
printf 'case\texit_code\n' > "$OUT/status.tsv"
common=(max_step=192 stop_time=100000 amr.n_cell=32 32 32 amr.max_level=2
    amr.max_grid_size=16 16 16 amr.blocking_factor_x=8 8 8
    amr.blocking_factor_y=8 8 8 amr.blocking_factor_z=8 8 8 amr.n_proper=2
    amr.regrid_int=32 amr.plot_int=-1 checkpoint.begin_step=0 checkpoint.chk_int=64
    checkpoint.keep_latest_only=false checkpoint.write_particles=false
    verification.convergence_enabled=false verification.final_state_check=false
    verification.osi_ab_check=false verification.osi_seed_pattern=false
    verification.refinement_diagnostics=false performance.report_int=32
    performance.osi_detail_timing=0 lbm.osi_mpi_pipeline_chunk_bytes=0)
run() {
    local name=$1 ranks=$2 app=$3
    shift 3
    mkdir "$OUT/$name"
    cd "$OUT/$name"
    osi_record_command command.txt "$OUT/$app.ex" "$OUT/inputs" "${common[@]}" "$@"
    local code=0
    osi_launch_mpi hmpi "$OUT/hostfile" "$ranks" "$ranks" "$OUT/$app.ex" "$OUT/inputs" \
        "${common[@]}" "$@" > run.log 2>&1 || code=$?
    printf '%s\t%s\n' "$name" "$code" >> "$OUT/status.tsv"
    [[ "$code" == 0 ]]
}
suite="${REGRID_SUITE:-validation}"
printf '%s\n' "$suite" > "$OUT/suite.txt"
if [[ "$suite" == seeded ]]; then
    # 非均匀周期数据专门检验保留缓存中的地址、相位及 MPI 映射。
    for mode in 0 1 2; do
        for ranks in 1 2; do
            name="fixture_seeded_i${mode}_r${ranks}"
            extra=(verification.regrid_fixture=true verification.osi_seed_pattern=true
                lbm.stream_mode=1 lbm.interp_mode="$mode" geometry.is_periodic=1 1 1)
            run "${name}_baseline" "$ranks" baseline "${extra[@]}"
            run "${name}_candidate" "$ranks" candidate "${extra[@]}" \
                verification.ddf_reference_checkpoint="$OUT/${name}_baseline/chk00000192"
        done
        run "lockstep_seeded_i${mode}" 2 candidate "${extra[@]}" max_step=96 checkpoint.chk_int=-1 \
            verification.osi_ab_check=true verification.osi_ab_continue_on_mismatch=false \
            verification.osi_step_entry_check_interval=32
    done
elif [[ "$suite" != timing && "$suite" != steady ]]; then
    for ranks in 1 2; do
        for boundary in cavity periodic; do
            domain=(0 0 0)
            if [[ "$boundary" == periodic ]]; then domain=(1 1 1); fi
            for storage in 0 1; do
                name="fixture_r${ranks}_${boundary}_s${storage}"
                run "${name}_baseline" "$ranks" baseline verification.regrid_fixture=true \
                    lbm.stream_mode="$storage" geometry.is_periodic="${domain[@]}"
                run "${name}_candidate" "$ranks" candidate verification.regrid_fixture=true \
                    lbm.stream_mode="$storage" geometry.is_periodic="${domain[@]}" \
                    verification.ddf_reference_checkpoint="$OUT/${name}_baseline/chk00000192"
            done
        done
    done
    for mode in 0 1 2; do
        name="natural_i${mode}"
        run "${name}_baseline" 2 baseline lbm.stream_mode=1 lbm.interp_mode="$mode" \
            verification.refinement_diagnostics=true
        run "${name}_candidate" 2 candidate lbm.stream_mode=1 lbm.interp_mode="$mode" \
            verification.refinement_diagnostics=true \
            verification.ddf_reference_checkpoint="$OUT/${name}_baseline/chk00000192"
        run "lockstep_i${mode}" 2 candidate max_step=96 checkpoint.chk_int=-1 \
            lbm.stream_mode=1 lbm.interp_mode="$mode" geometry.is_periodic=1 1 1 \
            verification.regrid_fixture=true verification.osi_ab_check=true \
            verification.osi_ab_continue_on_mismatch=false verification.osi_step_entry_check_interval=32
    done
    for app in baseline candidate; do
        run "restart_$app" 2 "$app" verification.regrid_fixture=true lbm.stream_mode=1 \
            geometry.is_periodic=1 1 1 checkpoint.begin_step=64 checkpoint.chk_int=-1 \
            checkpoint.chk_prefix="$OUT/fixture_r2_periodic_s1_baseline/chk" \
            verification.ddf_reference_checkpoint="$OUT/fixture_r2_periodic_s1_baseline/chk00000192"
    done
    run detail 1 candidate max_step=96 checkpoint.chk_int=-1 lbm.stream_mode=1 \
        performance.regrid_detail_timing=true
else
    if [[ "$suite" == steady ]]; then
        restart="$TASK/job622345/fixture_seeded_i0_r1_baseline/chk"
        extra=(verification.regrid_fixture=true verification.osi_seed_pattern=true
            geometry.is_periodic=1 1 1 checkpoint.begin_step=192 checkpoint.chk_prefix="$restart"
            checkpoint.chk_int=-1 max_step=448 performance.report_int=64 lbm.stream_mode=1)
    else
        restart="$CASE/runs/a5_B_optimized_128000/job622137/B/chk"
        extra=(amr.n_cell=128 128 128 amr.max_grid_size=128 256 512
            amr.blocking_factor_x=16 16 16 amr.blocking_factor_y=16 16 16
            amr.blocking_factor_z=16 16 16 amr.n_proper=1 lbm.err=0.125 0.25 0.5 1
            checkpoint.begin_step=128000 checkpoint.chk_prefix="$restart" checkpoint.chk_int=-1
            max_step=128640 performance.report_int=160 lbm.stream_mode=1)
    fi
    sequence=(baseline candidate candidate baseline baseline candidate candidate baseline)
    for i in "${!sequence[@]}"; do
        run "timing_${i}_${sequence[$i]}" 1 "${sequence[$i]}" "${extra[@]}"
    done
    # 同一物理状态另测准备阶段；同步诊断不进入上面的整体收益统计。
    if [[ "$suite" == timing ]]; then
        run timing_detail 1 candidate "${extra[@]}" max_step=128160 performance.regrid_detail_timing=true
    fi
fi
"$REPO/.venv/bin/python" "$OUT/analyze_regrid_cache_reuse.py" "$OUT" "$suite"
