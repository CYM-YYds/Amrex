#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_osi_mpi_direct_multilevel_smoke
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=2
#DSUB -N 1
#DSUB -o logs/submit/%J-osi-mpi-direct-multilevel-smoke.log
#DSUB -e logs/submit/%J-osi-mpi-direct-multilevel-smoke.log
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
APP_EXE="${CASE_DIR}/main3d.gnu.TPROF.MPI.CUDA.ex"
INPUTS="${CASE_DIR}/config/inputs"

if [[ ! -x "${APP_EXE}" ]]; then
    echo "ERROR: executable not found: ${APP_EXE}" >&2
    exit 1
fi
if [[ -z "${CCS_ALLOC_FILE:-}" || ! -r "${CCS_ALLOC_FILE}" ]]; then
    echo "ERROR: CCS_ALLOC_FILE is unavailable; submit through dsub." >&2
    exit 1
fi

HOSTFILE="$(mktemp /tmp/box3d_osi_mpi_direct_multilevel.XXXXXX)"
trap 'rm -f "${HOSTFILE}"' EXIT
awk '{ if (length($1) > 0 && length($2) > 0) print $1 " slots=" $2 }' \
    "${CCS_ALLOC_FILE}" > "${HOSTFILE}"

echo "osi_mpi_direct_multilevel_smoke: ranks=2 gpus=2 steps=64 max_level=2 regrid_int=32"
mpirun \
    -hostfile "${HOSTFILE}" \
    -n 2 \
    -npernode 2 \
    -x PATH -x LD_LIBRARY_PATH \
    --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
    bash -lc 'export CUDA_VISIBLE_DEVICES=${OMPI_COMM_WORLD_LOCAL_RANK:-${MPI_LOCALRANKID:-0}}; exec "$@"' bash \
    "${APP_EXE}" "${INPUTS}" \
    max_step=64 \
    performance.report_int=32 \
    amr.max_level=2 \
    amr.n_cell=64 64 64 \
    amr.max_grid_size=64 64 64 \
    amr.blocking_factor_x=32 32 32 \
    amr.blocking_factor_y=32 32 32 \
    amr.blocking_factor_z=32 32 32 \
    amr.regrid_int=32 \
    amr.plot_int=-1 \
    checkpoint.chk_int=-1 \
    lbm.stream_mode=1 \
    lbm.collide_mode=1 \
    lbm.osi_local_direct=1 \
    lbm.osi_parallel_copy=1 \
    lbm.osi_mpi_direct=1 \
    lbm.osi_mpi_device_direct=0 \
    lbm.osi_mpi_pipeline_chunk_bytes=2097152 \
    verification.convergence_enabled=false \
    verification.osi_seed_pattern=false \
    verification.osi_ab_check=true \
    verification.osi_ab_continue_on_mismatch=false \
    verification.osi_step_entry_check_interval=1 \
    verification.osi_check_after_average_valid=true \
    verification.check_after_first_fill=true
