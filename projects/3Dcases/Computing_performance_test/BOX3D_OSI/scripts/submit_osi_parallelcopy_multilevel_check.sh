#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_osi_parallelcopy_check
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=2
#DSUB -N 1
#DSUB -o logs/submit/%J-osi-parallelcopy-check.log
#DSUB -e logs/submit/%J-osi-parallelcopy-check.log
#DSUB -l cuda122

set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1
module load compilers/cuda/12.8.0
module load compilers/gcc/11.3.0

CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_EXE="${CASE_DIR}/main3d.gnu.TPROF.MPI.CUDA.ex"
INPUTS="${CASE_DIR}/config/inputs"
HOSTFILE="$(mktemp /tmp/box3d_osi_parallelcopy_check.XXXXXX)"
trap 'rm -f "${HOSTFILE}"' EXIT

[[ -x "${APP_EXE}" ]] || { echo "ERROR: executable missing" >&2; exit 1; }
[[ -r "${CCS_ALLOC_FILE:-}" ]] || { echo "ERROR: CCS_ALLOC_FILE missing" >&2; exit 1; }
awk '{ if (length($1) > 0 && length($2) > 0) print $1 " slots=" $2 }' \
    "${CCS_ALLOC_FILE}" > "${HOSTFILE}"

RUN_DIR="${CASE_DIR}/tmp/osi_parallelcopy_check_${CCS_JOB_ID:-manual}"
mkdir -p "${RUN_DIR}/seed" "${RUN_DIR}/ab" "${RUN_DIR}/osi"

run_case() {
    local label=$1
    local run_dir=$2
    shift 2
    echo "osi_parallelcopy_check_begin: mode=${label}"
    cd "${run_dir}"
    mpirun -hostfile "${HOSTFILE}" -n 2 -npernode 2 \
        -x PATH -x LD_LIBRARY_PATH \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
        bash -lc 'export CUDA_VISIBLE_DEVICES=${OMPI_COMM_WORLD_LOCAL_RANK:-${MPI_LOCALRANKID:-0}}; exec "$@"' bash \
        "${APP_EXE}" "${INPUTS}" \
        amr.max_level=1 amr.max_grid_size="128 128" \
        amr.blocking_factor_x="32 32" amr.blocking_factor_y="32 32" \
        amr.blocking_factor_z="32 32" amr.plot_int=-1 checkpoint.keep_latest_only=false \
        checkpoint.write_particles=false verification.convergence_enabled=false \
        amr.regrid_int=32 "$@"
    echo "osi_parallelcopy_check_end: mode=${label}"
}

run_case SEED "${RUN_DIR}/seed" \
    max_step=32 performance.report_int=32 lbm.stream_mode=0 \
    checkpoint.begin_step=0 checkpoint.chk_int=32 checkpoint.chk_prefix=chk

ln -s "${RUN_DIR}/seed/chk00000032" "${RUN_DIR}/ab/ab_chk00000032"
run_case AB "${RUN_DIR}/ab" \
    max_step=40 performance.report_int=8 amr.regrid_int=-1 lbm.stream_mode=0 \
    checkpoint.begin_step=32 checkpoint.chk_int=40 checkpoint.chk_prefix=ab_chk

ln -s "${RUN_DIR}/seed/chk00000032" "${RUN_DIR}/osi/osi_chk00000032"
run_case OSI_PARALLELCOPY "${RUN_DIR}/osi" \
    max_step=40 performance.report_int=8 amr.regrid_int=-1 lbm.stream_mode=1 \
    lbm.osi_local_direct=1 lbm.osi_mpi_direct=1 lbm.osi_parallel_copy=1 \
    lbm.osi_mpi_device_direct=1 amrex.use_gpu_aware_mpi=1 \
    lbm.osi_mpi_pipeline_chunk_bytes=0 \
    verification.osi_ab_check=false verification.osi_seed_pattern=false \
    checkpoint.begin_step=32 checkpoint.chk_int=40 checkpoint.chk_prefix=osi_chk

run_case COMPARE "${RUN_DIR}/osi" \
    max_step=40 performance.report_int=40 amr.regrid_int=-1 lbm.stream_mode=1 \
    lbm.osi_local_direct=1 lbm.osi_mpi_direct=1 lbm.osi_parallel_copy=1 \
    lbm.osi_mpi_device_direct=1 amrex.use_gpu_aware_mpi=1 \
    checkpoint.begin_step=40 checkpoint.chk_int=-1 checkpoint.chk_prefix=osi_chk \
    verification.ddf_reference_checkpoint="${RUN_DIR}/ab/ab_chk00000040"

echo "osi_parallelcopy_check_artifacts: run_dir=${RUN_DIR}"
