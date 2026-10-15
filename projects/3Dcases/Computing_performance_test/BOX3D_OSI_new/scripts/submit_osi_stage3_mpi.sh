#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_osi_stage3_mpi
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=2
#DSUB -N 1
#DSUB -o logs/submit/%J-osi-stage3-mpi.log
#DSUB -e logs/submit/%J-osi-stage3-mpi.log
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
cd "${CASE_DIR}"

APP_EXE=./main3d.gnu.TPROF.MPI.CUDA.ex
if [[ ! -x "${APP_EXE}" ]]; then
    echo "ERROR: ${APP_EXE} is missing; compile BOX3D_OSI first." >&2
    exit 1
fi
if [[ -z "${CCS_ALLOC_FILE:-}" || ! -r "${CCS_ALLOC_FILE}" ]]; then
    echo "ERROR: CCS_ALLOC_FILE is unavailable; submit through dsub." >&2
    exit 1
fi

HOSTFILE="$(mktemp /tmp/box3d_osi_stage3_mpi.XXXXXX)"
trap 'rm -f "${HOSTFILE}"' EXIT
awk '{ if (length($1) > 0 && length($2) > 0) print $1 " slots=" $2 }' \
    "${CCS_ALLOC_FILE}" > "${HOSTFILE}"
if [[ ! -s "${HOSTFILE}" ]]; then
    echo "ERROR: no hosts found in CCS_ALLOC_FILE." >&2
    exit 1
fi

mpirun \
    -hostfile "${HOSTFILE}" \
    -n 2 \
    -npernode 2 \
    -x PATH -x LD_LIBRARY_PATH \
    --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
    bash -lc 'export CUDA_VISIBLE_DEVICES=${OMPI_COMM_WORLD_LOCAL_RANK:-${MPI_LOCALRANKID:-0}}; exec ./main3d.gnu.TPROF.MPI.CUDA.ex config/inputs max_step=64 performance.report_int=64 amr.max_level=0 amr.max_grid_size=64 amr.blocking_factor_x=64 amr.blocking_factor_y=64 amr.blocking_factor_z=64 geometry.is_periodic=1 1 1 amr.regrid_int=-1 amr.plot_int=-1 checkpoint.chk_int=-1 verification.convergence_enabled=false verification.osi_seed_pattern=true verification.osi_ab_check=true lbm.stream_mode=1 lbm.osi_local_direct=1 lbm.osi_mpi_direct=1 ${AMREX_RUN_ARGS:-}'
