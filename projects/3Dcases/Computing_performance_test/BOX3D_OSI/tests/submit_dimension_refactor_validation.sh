#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n lbm_dim_refactor
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=16;mem=49152;gpu=2
#DSUB -N 1
#DSUB -o logs/submit/%J-dim-refactor.log
#DSUB -e logs/submit/%J-dim-refactor.log
#DSUB -l cuda122
# 复用现有阶段诊断，对照修改前后程序；本脚本不实现额外数值比较内核。
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="$(git -C "$CASE_DIR" rev-parse --show-toplevel)"
BASELINE_DIR="${DIM_REFACTOR_BASELINE:-$CASE_DIR/runs/accessor_refactor_20261019/baseline}"
RUN_DIR="$CASE_DIR/runs/dimension_refactor_${CCS_JOB_ID:?}"
mkdir -p "$RUN_DIR"
cp -a "$CASE_DIR/src" "$CASE_DIR/config" "$RUN_DIR/"
cp "$CASE_DIR/tests/submit_dimension_refactor_validation.sh" "$CASE_DIR/tests/check_dimension_refactor.py" "$RUN_DIR/"
for dim in 2 3; do
    cp "$CASE_DIR/main${dim}d.gnu.TPROF.MPI.CUDA.ex" "$RUN_DIR/app${dim}d.ex"
    cp "$BASELINE_DIR/app${dim}d.ex" "$RUN_DIR/baseline${dim}d.ex"
done
cp "$BASELINE_DIR/git_head.txt" "$RUN_DIR/baseline_git_head.txt"
git -C "$REPO_DIR" rev-parse HEAD > "$RUN_DIR/git_head.txt"
git -C "$REPO_DIR" diff -- "$CASE_DIR" > "$RUN_DIR/source.diff"
sha256sum "$RUN_DIR"/*.ex > "$RUN_DIR/executable.sha256"
awk 'NF >= 2 {print $1 " slots=" $2}' "${CCS_ALLOC_FILE:?}" > "$RUN_DIR/hostfile"
printf 'case\texit_code\n' > "$RUN_DIR/status.tsv"
run() {
    local name=$1 ranks=$2 executable=$3 input=$4
    shift 4
    mkdir "$RUN_DIR/$name"
    cd "$RUN_DIR/$name"
    printf '%q ' "$executable" "$input" "$@" > command.txt
    printf '\n' >> command.txt
    local result=0
    mpirun -hostfile "$RUN_DIR/hostfile" -n "$ranks" -npernode "$ranks" -x PATH -x LD_LIBRARY_PATH \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart /usr/bin/env -u BASH_ENV \
        bash --noprofile --norc -c \
        'export CUDA_VISIBLE_DEVICES=${OMPI_COMM_WORLD_LOCAL_RANK:-0}; exec "$@"' bash \
        "$executable" "$input" "$@" > run.log 2>&1 || result=$?
    printf '%s\t%s\n' "$name" "$result" >> "$RUN_DIR/status.tsv"
    echo "REFACTOR_CASE $name exit=$result"
}
common=(max_step=192 amr.regrid_int=32 amr.plot_int=-1 checkpoint.chk_int=192
    checkpoint.keep_latest_only=false checkpoint.write_particles=false
    verification.convergence_enabled=false verification.final_state_check=true
    verification.regrid_fixture=true performance.report_int=192 lbm.osi_mpi_pipeline_chunk_bytes=0)
checks=(verification.osi_ab_check=true verification.osi_ab_continue_on_mismatch=false
    verification.osi_step_entry_check_interval=1 verification.osi_check_after_average_valid=true
    verification.check_after_first_fill=true)
for dim in 2 3; do
    input="$RUN_DIR/config/inputs"
    mesh=(amr.n_cell=32 32 32 amr.max_level=1 amr.max_grid_size=16 16
        amr.blocking_factor_x=8 8 amr.blocking_factor_y=8 8 amr.blocking_factor_z=8 8 amr.n_proper=2)
    cavity=(0 0 0); periodic=(1 1 1)
    if [[ "$dim" == 2 ]]; then
        input="$input.2d"
        mesh=(amr.n_cell=64 64 amr.max_level=2 amr.max_grid_size=32 32 32
            amr.blocking_factor_x=16 16 16 amr.blocking_factor_y=16 16 16 amr.n_proper=2)
        cavity=(0 0); periodic=(1 1)
    fi
    for mode in 0 1 2; do
        for ranks in 1 2; do
            for boundary in cavity periodic; do
                domain=("${cavity[@]}"); seed=false
                if [[ "$boundary" == periodic ]]; then domain=("${periodic[@]}"); seed=true; fi
                run "d${dim}_i${mode}_r${ranks}_${boundary}" "$ranks" "$RUN_DIR/app${dim}d.ex" "$input" \
                    "${common[@]}" "${mesh[@]}" "${checks[@]}" max_step=64 checkpoint.chk_int=-1 lbm.stream_mode=1 lbm.interp_mode="$mode" \
                    geometry.is_periodic="${domain[@]}" verification.osi_seed_pattern="$seed"
                if [[ "$boundary" == cavity ]]; then
                    run "d${dim}_i${mode}_r${ranks}_${boundary}_control" "$ranks" "$RUN_DIR/baseline${dim}d.ex" "$input" \
                        "${common[@]}" "${mesh[@]}" "${checks[@]}" max_step=64 checkpoint.chk_int=-1 \
                        lbm.stream_mode=1 lbm.interp_mode="$mode" geometry.is_periodic="${domain[@]}" verification.osi_seed_pattern="$seed"
                fi
            done
        done
        run "d${dim}_i${mode}_baseline" 2 "$RUN_DIR/baseline${dim}d.ex" "$input" \
            "${common[@]}" "${mesh[@]}" lbm.stream_mode=0 lbm.interp_mode="$mode"
        for storage in 0 1; do
            run "d${dim}_i${mode}_current_s${storage}" 2 "$RUN_DIR/app${dim}d.ex" "$input" \
                "${common[@]}" "${mesh[@]}" lbm.stream_mode="$storage" lbm.interp_mode="$mode" \
                verification.ddf_reference_checkpoint="$RUN_DIR/d${dim}_i${mode}_baseline/chk00000192"
        done
    done
done
"$REPO_DIR/.venv/bin/python" "$RUN_DIR/check_dimension_refactor.py" "$RUN_DIR"
