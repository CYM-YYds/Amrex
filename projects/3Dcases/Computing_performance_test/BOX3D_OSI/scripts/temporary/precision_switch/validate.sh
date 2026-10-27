#!/usr/bin/env bash
# 本次精度切换的临时 CUDA 数值验证；保留于此用于复现本次报告。
#DSUB --job_type cosched
#DSUB -n box3d_precision
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=2
#DSUB -N 1
#DSUB -o logs/submit/%J-precision.log
#DSUB -e logs/submit/%J-precision.log
#DSUB -l cuda122
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
CASE_DIR=$(cd "$(dirname "$0")/../../.." && pwd)
ROOT="$CASE_DIR/runs/temporary/precision_switch"
OUT="$ROOT/job${CCS_JOB_ID:?}"
mkdir -p "$OUT"
cp "$CASE_DIR/config/inputs" "$OUT/inputs"
cp -a "$CASE_DIR/src" "$CASE_DIR/config" "$OUT/"
git -C "$CASE_DIR" rev-parse HEAD > "$OUT/git_head.txt"
git -C "$CASE_DIR" diff > "$OUT/source.diff"
cp "$ROOT/baseline/main3d.gnu.TPROF.MPI.CUDA.ex" "$OUT/baseline.ex"
cp "$CASE_DIR/main3d.gnu.TPROF.MPI.CUDA.ex" "$OUT/double.ex"
cp "$CASE_DIR/main3d.gnu.FLOAT.TPROF.MPI.CUDA.ex" "$OUT/float.ex"
sha256sum "$OUT"/*.ex > "$OUT/executable.sha256"
awk 'NF >= 2 {print $1 " slots=" $2}' "${CCS_ALLOC_FILE:?}" > "$OUT/hostfile"
printf 'case\texit_code\n' > "$OUT/status.tsv"
common=(max_step=64 amr.n_cell=32 32 32 amr.max_level=1 amr.max_grid_size=16 16
    amr.blocking_factor_x=8 8 amr.blocking_factor_y=8 8 amr.blocking_factor_z=8 8 amr.n_proper=2
    amr.regrid_int=16 amr.plot_int=-1 checkpoint.chk_int=64 checkpoint.keep_latest_only=false
    checkpoint.write_particles=false verification.convergence_enabled=false verification.final_state_check=true
    verification.regrid_fixture=true performance.report_int=64 lbm.osi_mpi_pipeline_chunk_bytes=0)
run() {
    local name=$1 app=$2 ranks=$3
    shift 3
    mkdir -p "$OUT/$name"
    cd "$OUT/$name"
    printf '%q ' "$app" "$OUT/inputs" "${common[@]}" "$@" > command.txt
    printf '\n' >> command.txt
    local code=0
    mpirun -hostfile "$OUT/hostfile" -n "$ranks" -npernode "$ranks" -x PATH -x LD_LIBRARY_PATH \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart /usr/bin/env -u BASH_ENV \
        bash --noprofile --norc -c 'export CUDA_VISIBLE_DEVICES=${OMPI_COMM_WORLD_LOCAL_RANK:-0}; exec "$@"' bash \
        "$app" "$OUT/inputs" "${common[@]}" "$@" > run.log 2>&1 || code=$?
    printf '%s\t%s\n' "$name" "$code" >> "$OUT/status.tsv"
    echo "$name exit=$code"
}
for mode in 0 1 2; do
    run "baseline_i$mode" "$OUT/baseline.ex" 2 lbm.interp_mode="$mode" lbm.stream_mode=1
    run "double_i$mode" "$OUT/double.ex" 2 lbm.interp_mode="$mode" lbm.stream_mode=1 \
        verification.ddf_reference_checkpoint="$OUT/baseline_i$mode/chk00000064"
    run "float_i$mode" "$OUT/float.ex" 2 lbm.interp_mode="$mode" lbm.stream_mode=1
    # 用双精度读取两份 checkpoint，避免单精度的误差归约掩盖差异。
    run "compare_i$mode" "$OUT/double.ex" 1 max_step=64 checkpoint.begin_step=64 \
        checkpoint.chk_prefix="$OUT/float_i$mode/chk" checkpoint.chk_int=-1 \
        verification.ddf_reference_checkpoint="$OUT/double_i$mode/chk00000064"
    run "float_stage_i$mode" "$OUT/float.ex" 2 lbm.interp_mode="$mode" lbm.stream_mode=1 \
        amr.max_level=0 geometry.is_periodic=1 1 1 verification.regrid_fixture=false \
        verification.osi_seed_pattern=true verification.osi_ab_check=true \
        verification.osi_ab_continue_on_mismatch=false verification.osi_step_entry_check_interval=1 \
        verification.osi_check_after_average_valid=true verification.check_after_first_fill=true
 done
