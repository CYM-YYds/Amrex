#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_stream_smoke
#DSUB -A root.huazkjdxmrsgjzdsyshi
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-stream-smoke.log
#DSUB -e logs/submit/%J-stream-smoke.log
#DSUB -l cuda122

set -euo pipefail

source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1
module load compilers/cuda/12.1.0
module load compilers/gcc/10.3.1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${CASE_DIR}"

APP_EXE="./main3d.gnu.TPROF.MPI.CUDA.ex"
if [[ ! -x "${APP_EXE}" ]]; then
    echo "ERROR: ${APP_EXE} is missing or not executable." >&2
    exit 1
fi

if [[ -z "${CCS_ALLOC_FILE:-}" || ! -r "${CCS_ALLOC_FILE}" ]]; then
    echo "ERROR: CCS_ALLOC_FILE is unavailable. Submit this script through dsub." >&2
    exit 1
fi

HOSTFILE="$(mktemp /tmp/box3d_stream_smoke.XXXXXX)"
cleanup() {
    rm -f "${HOSTFILE}"
}
trap cleanup EXIT

awk '
{
    if (length($1) > 0 && length($2) > 0) {
        print $1 " slots=" $2
    }
}
' "${CCS_ALLOC_FILE}" > "${HOSTFILE}"

if [[ ! -s "${HOSTFILE}" ]]; then
    echo "ERROR: no hosts were found in CCS_ALLOC_FILE." >&2
    exit 1
fi

echo "BOX3D Stream smoke test: 64 steps, one GPU"
mpirun \
    -hostfile "${HOSTFILE}" \
    -n 1 \
    -x PATH -x LD_LIBRARY_PATH \
    --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
    bash -lc 'export CUDA_VISIBLE_DEVICES=${OMPI_COMM_WORLD_LOCAL_RANK:-${MPI_LOCALRANKID:-0}}; exec ./main3d.gnu.TPROF.MPI.CUDA.ex config/inputs max_step=64 amr.plot_int=100000 checkpoint.chk_int=-1'
