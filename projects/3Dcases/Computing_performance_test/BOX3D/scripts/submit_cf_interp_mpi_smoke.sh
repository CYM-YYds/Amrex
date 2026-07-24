#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_cf_interp_mpi_smoke
#DSUB -A root.huazkjdxmrsgjzdsyshi
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=2
#DSUB -N 1
#DSUB -o logs/submit/%J-cf-interp-mpi-smoke.log
#DSUB -e logs/submit/%J-cf-interp-mpi-smoke.log
#DSUB -l cuda122

set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1
module load compilers/cuda/12.1.0
module load compilers/gcc/11.3.0

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${CASE_DIR}"

HOSTFILE="/tmp/box3d-cf-interp-mpi-hostfile.$$"
trap 'rm -f "${HOSTFILE}"' EXIT
awk -v hostfile="${HOSTFILE}" \
    '{ if (length($1) > 0 && length($3) > 0) print $1 " slots=" $2 >> hostfile }' \
    "${CCS_ALLOC_FILE}"

mpirun \
    -hostfile "${HOSTFILE}" \
    -npernode 2 \
    -x PATH -x LD_LIBRARY_PATH \
    --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
    bash -lc 'export CUDA_VISIBLE_DEVICES=${OMPI_COMM_WORLD_LOCAL_RANK:-${MPI_LOCALRANKID:-0}}; exec ./main3d.gnu.TPROF.MPI.CUDA.ex config/inputs max_step=64 amr.plot_int=1000000 checkpoint.chk_int=-1 lbm.cf_interp_mode=2'
