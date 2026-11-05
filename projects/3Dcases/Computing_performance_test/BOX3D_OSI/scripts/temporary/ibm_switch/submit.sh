#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n osi_ibm_switch
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=16;mem=49152;gpu=2
#DSUB -N 1
#DSUB -o runs/temporary/ibm_switch/%J.log
#DSUB -e runs/temporary/ibm_switch/%J.log
#DSUB -l cuda122
# 本任务临时回归入口；保留输入、命令、日志和 checkpoint 以复现逐值结论。
set -euo pipefail
ulimit -c 0
CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
REPO_DIR="$(git -C "$CASE_DIR" rev-parse --show-toplevel)"
TASK_DIR="$CASE_DIR/runs/temporary/ibm_switch${IBM_VALIDATION_ROUND:+/$IBM_VALIDATION_ROUND}"
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
awk 'NF >= 2 {print $1 " slots=" $2}' "${CCS_ALLOC_FILE:?}" > "$TASK_DIR/hostfile"
if [[ ${IBM_VALIDATION_STAGE:-matrix} == matrix ]]; then
    printf 'case\texit_code\n' > "$TASK_DIR/status.tsv"
fi
run_case() {
    local name=$1 ranks=$2 exe=$3
    shift 3
    mkdir -p "$TASK_DIR/$name/data"
    cd "$TASK_DIR/$name"
    cp "$TASK_DIR/inputs" inputs
    printf '%q ' "$exe" inputs "${common[@]}" "${mesh[@]}" "$@" > command.txt
    printf '\n' >> command.txt
    local result=0
    mpirun -hostfile "$TASK_DIR/hostfile" -n "$ranks" -npernode "$ranks" \
        -x PATH -x LD_LIBRARY_PATH --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
        /usr/bin/env -u BASH_ENV bash --noprofile --norc -c \
        'export CUDA_VISIBLE_DEVICES=${OMPI_COMM_WORLD_LOCAL_RANK:-0}; exec "$@"' bash \
        "$exe" inputs "${common[@]}" "${mesh[@]}" "$@" > run.log 2>&1 || result=$?
    printf '%s\t%s\n' "$name" "$result" >> "$TASK_DIR/status.tsv"
    echo "IBM_CASE $name exit=$result"
}
common=(max_step=32 lbm.reynolds_number=100 amr.regrid_int=8 amr.plot_int=32 checkpoint.chk_int=16
    checkpoint.keep_latest_only=false checkpoint.write_particles=false
    verification.convergence_enabled=false verification.final_state_check=true performance.report_int=32
    lbm.osi_mpi_pipeline_chunk_bytes=0 lbm.osi_collide_comm_overlap=0)
mesh=(amr.n_cell=32 32 32 amr.max_level=1 amr.max_grid_size=16 16 amr.blocking_factor_x=8 8
    amr.blocking_factor_y=8 8 amr.blocking_factor_z=8 8 lbm.err=0.1 0.2)
app="$TASK_DIR/app.ex"
if [[ ${IBM_VALIDATION_STAGE:-matrix} == lockstep ]]; then
    mv "$TASK_DIR/cylinder_lockstep" "$TASK_DIR/initial_lockstep"
    run_case cylinder_lockstep 1 "$app" ibm.enabled=true particles.shape=cylinder verification.osi_ab_check=true \
        verification.osi_step_entry_check_interval=8 verification.osi_check_after_average_valid=true \
        verification.check_after_first_fill=true
    "$REPO_DIR/.venv/bin/python" "$CASE_DIR/scripts/temporary/ibm_switch/verify.py" "$TASK_DIR"
    exit 0
fi
run_case baseline 1 "$TASK_DIR/baseline.ex"
run_case off 1 "$app" ibm.enabled=false
run_case sphere 1 "$app" ibm.enabled=true particles.shape=sphere
run_case cylinder_ab 1 "$app" ibm.enabled=true particles.shape=cylinder lbm.stream_mode=0
run_case cylinder_osi 1 "$app" ibm.enabled=true particles.shape=cylinder lbm.stream_mode=1
run_case cylinder_lockstep 1 "$app" ibm.enabled=true particles.shape=cylinder verification.osi_ab_check=true \
    verification.osi_step_entry_check_interval=8 verification.osi_check_after_average_valid=true \
    verification.check_after_first_fill=true
run_case cylinder_mpi 2 "$app" ibm.enabled=true particles.shape=cylinder
run_case cylinder_mpi_overlap 2 "$app" ibm.enabled=true particles.shape=cylinder \
    lbm.osi_collide_comm_overlap=1 lbm.osi_mpi_pipeline_chunk_bytes=2097152
run_case cylinder_level2 1 "$app" ibm.enabled=true particles.shape=cylinder amr.max_level=2 \
    amr.max_grid_size=16 16 16 amr.blocking_factor_x=8 8 8 amr.blocking_factor_y=8 8 8 \
    amr.blocking_factor_z=8 8 8 lbm.err=0.1 0.2 0.4
run_case uniform_off 1 "$app" ibm.enabled=false amr.max_level=0
run_case uniform_cylinder 1 "$app" ibm.enabled=true particles.shape=cylinder amr.max_level=0
for name in sphere cylinder_osi off; do
    mkdir -p "$TASK_DIR/${name}_restart"
    ln -s "$TASK_DIR/$name/chk00000016" "$TASK_DIR/${name}_restart/chk00000016"
    run_case "${name}_restart" 1 "$app" ibm.enabled="$( [[ $name == off ]] && echo false || echo true )" \
        "particles.shape=$( [[ $name == cylinder_osi ]] && echo cylinder || echo sphere )" \
        checkpoint.begin_step=16
done
mkdir -p "$TASK_DIR/legacy_restart"
ln -s "$TASK_DIR/baseline/chk00000016" "$TASK_DIR/legacy_restart/chk00000016"
run_case legacy_restart 1 "$app" ibm.enabled=false checkpoint.begin_step=16
run_case reject_metadata 1 "$app" ibm.enabled=true particles.shape=cylinder ibm.radius=0.13 \
    checkpoint.begin_step=16 checkpoint.chk_prefix="$TASK_DIR/cylinder_osi/chk"
run_case reject_checkpoint 1 "$app" ibm.enabled=true checkpoint.begin_step=16 checkpoint.chk_prefix="$TASK_DIR/off/chk"
run_case reject_shape 1 "$app" ibm.enabled=true particles.shape=invalid
run_case reject_mode 1 "$app" ibm.enabled=true lbm.collide_mode=0 lbm.stream_mode=0
run_case reject_origin 1 "$app" ibm.enabled=true geometry.prob_lo=1 1 1 geometry.prob_hi=2 2 2
run_case reject_resolution 1 "$app" ibm.enabled=true ibm.radius=0.0001
run_case reject_geometry 1 "$app" ibm.enabled=true ibm.radius=-1
"$REPO_DIR/.venv/bin/python" "$CASE_DIR/scripts/temporary/ibm_switch/verify.py" "$TASK_DIR"
