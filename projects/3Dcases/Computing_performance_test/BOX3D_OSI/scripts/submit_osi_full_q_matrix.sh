#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_osi_full_q_matrix
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=2
#DSUB -N 1
#DSUB -o logs/submit/%J-osi-full-q-matrix.log
#DSUB -e logs/submit/%J-osi-full-q-matrix.log
#DSUB -l cuda122

set -euo pipefail

source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1
module load compilers/cuda/12.8.0
module load compilers/gcc/11.3.0

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
RUN_DIR="${CASE_DIR}/runs/full_q_matrix_${CCS_JOB_ID:?}"
mkdir "${RUN_DIR}"
# 冻结可执行文件、输入和工作区源码差异，使每组测试可复现。
cp "${CASE_DIR}/main3d.gnu.TPROF.MPI.CUDA.ex" "${RUN_DIR}/app.ex"
cp "${CASE_DIR}/config/inputs" "${RUN_DIR}/inputs"
chmod a-w "${RUN_DIR}/inputs" "${RUN_DIR}/app.ex"
git -C "${CASE_DIR}" rev-parse HEAD > "${RUN_DIR}/git_head.txt"
git -C "${CASE_DIR}" diff -- src config scripts README.md > "${RUN_DIR}/source.diff"
sha256sum "${RUN_DIR}/app.ex" > "${RUN_DIR}/executable.sha256"
if [[ -z "${CCS_ALLOC_FILE:-}" || ! -r "${CCS_ALLOC_FILE}" ]]; then
    echo "ERROR: CCS_ALLOC_FILE unavailable" >&2
    exit 1
fi
HOSTFILE="${RUN_DIR}/hostfile"
awk '{ if (length($1) > 0 && length($2) > 0) print $1 " slots=" $2 }' "${CCS_ALLOC_FILE}" > "${HOSTFILE}"

run_group() {
    local group="$1"
    shift
    local ranks=2
    if [[ "${group}" == single ]]; then ranks=1; fi
    mkdir "${RUN_DIR}/${group}"
    cd "${RUN_DIR}/${group}"
    printf '%q ' "${RUN_DIR}/app.ex" "${RUN_DIR}/inputs" "$@" > command.txt
    printf '\n' >> command.txt
    echo "full_q_matrix_begin: group=${group} ranks=${ranks} gpus=${ranks}"
    mpirun -hostfile "${HOSTFILE}" -n "${ranks}" -npernode "${ranks}" \
        -x PATH -x LD_LIBRARY_PATH \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
        bash -lc 'export CUDA_VISIBLE_DEVICES=${OMPI_COMM_WORLD_LOCAL_RANK:-${MPI_LOCALRANKID:-0}}; exec "$@"' bash \
        "${RUN_DIR}/app.ex" "${RUN_DIR}/inputs" \
        max_step=64 performance.report_int=32 amr.max_level=2 \
        amr.n_cell=64 64 64 amr.max_grid_size=64 64 64 \
        amr.blocking_factor_x=32 32 32 amr.blocking_factor_y=32 32 32 \
        amr.blocking_factor_z=32 32 32 amr.regrid_int=32 amr.plot_int=-1 \
        checkpoint.chk_int=-1 checkpoint.chk_prefix=chk checkpoint.write_particles=0 \
        lbm.collide_mode=1 lbm.osi_mpi_device_direct=0 \
        verification.convergence_enabled=false verification.osi_seed_pattern=false \
        verification.osi_ab_continue_on_mismatch=false \
        verification.osi_step_entry_check_interval=1 \
        verification.osi_check_after_average_valid=true \
        verification.check_after_first_fill=true "$@" > run.log 2>&1
    echo "full_q_matrix_end: group=${group} exit=0 log=${RUN_DIR}/${group}/run.log"
}

run_group ab lbm.stream_mode=0 verification.osi_ab_check=false \
    checkpoint.chk_int=64
run_group direct lbm.stream_mode=1 verification.osi_ab_check=true \
    lbm.osi_local_direct=1 lbm.osi_parallel_copy=1 lbm.osi_mpi_direct=1 \
    lbm.osi_mpi_pipeline_chunk_bytes=2097152 \
    verification.ddf_reference_checkpoint="${RUN_DIR}/ab/chk00000064"
run_group single lbm.stream_mode=1 verification.osi_ab_check=true \
    lbm.osi_mpi_pipeline_chunk_bytes=0
run_group periodic lbm.stream_mode=1 verification.osi_ab_check=true \
    amr.max_level=0 amr.regrid_int=-1 amr.max_grid_size=32 \
    geometry.is_periodic=1 1 1 verification.osi_seed_pattern=true \
    lbm.osi_local_direct=1 lbm.osi_parallel_copy=1 lbm.osi_mpi_direct=1 \
    lbm.osi_mpi_pipeline_chunk_bytes=0

echo "full_q_matrix_completed: run_dir=${RUN_DIR}"
