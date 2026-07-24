#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_cf_interp_perf
#DSUB -A root.huazkjdxmrsgjzdsyshi
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-cf-interp-perf.log
#DSUB -e logs/submit/%J-cf-interp-perf.log
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

HOSTFILE="/tmp/box3d-cf-interp-perf-hostfile.$$"
trap 'rm -f "${HOSTFILE}"' EXIT
awk -v hostfile="${HOSTFILE}" \
    '{ if (length($1) > 0 && length($3) > 0) print $1 " slots=" $2 >> hostfile }' \
    "${CCS_ALLOC_FILE}"

run_case() {
    mpirun \
        -hostfile "${HOSTFILE}" \
        -npernode 1 \
        -x PATH -x LD_LIBRARY_PATH \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
        bash -lc "$1"
}

COMMON_ARGS="config/inputs max_step=1000 amr.plot_int=1000000 \
checkpoint.chk_int=-1 checkpoint.write_particles=0"

run_case "export CUDA_VISIBLE_DEVICES=0; exec ./main3d.gnu.TPROF.MPI.CUDA.ex \
${COMMON_ARGS} lbm.cf_interp_mode=2"

run_case "export CUDA_VISIBLE_DEVICES=0; exec ./main3d.gnu.TPROF.MPI.CUDA.ex \
${COMMON_ARGS} lbm.cf_interp_mode=3"
