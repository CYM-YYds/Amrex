#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_nghost_test
#DSUB -A root.huazkjdxmrsgjzdsyshi
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-nghost-test.log
#DSUB -e logs/submit/%J-nghost-test.log
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

HOSTFILE=/tmp/hostfile.$$
rm -f "${HOSTFILE}"
touch "${HOSTFILE}"

ntask=$(awk -v fff="${HOSTFILE}" '
{
    split($0, a, " ")
    if (length(a[1]) > 0 && length(a[3]) > 0) {
        print a[1]" slots="a[2] >> fff
        total_task += a[3]
    }
}
END { print total_task }
' "${CCS_ALLOC_FILE}")

NGPUS_PER_NODE=1
if [[ -n "${CUDA_VISIBLE_DEVICES:-}" ]]; then
    NGPUS_PER_NODE=$(echo "${CUDA_VISIBLE_DEVICES}" | awk -F',' '{print NF}')
elif command -v nvidia-smi >/dev/null 2>&1; then
    NGPUS_PER_NODE=$(nvidia-smi -L 2>/dev/null | wc -l)
fi
if [[ -z "${NGPUS_PER_NODE}" || "${NGPUS_PER_NODE}" -lt 1 ]]; then
    NGPUS_PER_NODE=1
fi

echo "nghost short test: ntask=${ntask}, NGPUS_PER_NODE=${NGPUS_PER_NODE}"

mpirun \
    -hostfile "${HOSTFILE}" \
    -npernode "${NGPUS_PER_NODE}" \
    -x PATH -x LD_LIBRARY_PATH \
    --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
    bash -lc 'export CUDA_VISIBLE_DEVICES=${OMPI_COMM_WORLD_LOCAL_RANK:-${MPI_LOCALRANKID:-0}}; exec ./main3d.gnu.MPI.CUDA.ex config/inputs max_step=64 amr.plot_int=100000 checkpoint.chk_int=-1'

ret=$?
rm -f "${HOSTFILE}"
exit "${ret}"
