#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_osi_sync_batch_mpi
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 2
#DSUB -o logs/submit/%J-osi-sync-batch-mpi.log
#DSUB -e logs/submit/%J-osi-sync-batch-mpi.log
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
mkdir -p logs/submit

APP_EXE=./main3d.gnu.TPROF.MPI.CUDA.ex
if [[ ! -x "${APP_EXE}" ]]; then
    echo "ERROR: ${APP_EXE} is missing; compile BOX3D_OSI first." >&2
    exit 1
fi

HOSTFILE="$(mktemp /tmp/box3d_osi_sync_batch_mpi.XXXXXX)"
trap 'rm -f "${HOSTFILE}"' EXIT
awk '{ if (length($1) > 0 && length($2) > 0) print $1 " slots=" $2 }' \
    "${CCS_ALLOC_FILE}" > "${HOSTFILE}"

for batch in 1 3 9; do
    echo "OSI_SYNC_BATCH_AB_BEGIN ranks=2 batch=${batch}"
    mpirun -hostfile "${HOSTFILE}" -n 2 -x PATH -x LD_LIBRARY_PATH \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
        bash -lc "export CUDA_VISIBLE_DEVICES=\${OMPI_COMM_WORLD_LOCAL_RANK:-\${MPI_LOCALRANKID:-0}}; exec ${APP_EXE} config/inputs_osi verification.osi_ab_check=true lbm.osi_sync_batch_components=${batch}"
    echo "OSI_SYNC_BATCH_AB_END ranks=2 batch=${batch}"
done

for batch in 1 3 9; do
    echo "OSI_SYNC_BATCH_PERF_BEGIN ranks=2 batch=${batch}"
    mpirun -hostfile "${HOSTFILE}" -n 2 -x PATH -x LD_LIBRARY_PATH \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
        bash -lc "export CUDA_VISIBLE_DEVICES=\${OMPI_COMM_WORLD_LOCAL_RANK:-\${MPI_LOCALRANKID:-0}}; exec ${APP_EXE} config/inputs_osi max_step=2000 stop_time=2000 verification.osi_ab_check=false lbm.osi_sync_batch_components=${batch}"
    echo "OSI_SYNC_BATCH_PERF_END ranks=2 batch=${batch}"
done
