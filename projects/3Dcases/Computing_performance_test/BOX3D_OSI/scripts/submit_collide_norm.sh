#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_collide_norm
#DSUB -A root.huazkjdxmrsgjzdsyshi
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-collide-norm.log
#DSUB -e logs/submit/%J-collide-norm.log
#DSUB -l cuda122

set -euo pipefail

source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1
module load compilers/cuda/12.8.0
module load compilers/gcc/12.2.0

case_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${case_dir}"

hostfile="$(mktemp /tmp/box3d-collide-norm.XXXXXX)"
trap 'rm -f "${hostfile}"' EXIT
awk '{ if (length($1) > 0 && length($2) > 0) print $1 " slots=" $2 }' \
    "${CCS_ALLOC_FILE}" > "${hostfile}"

run_case() {
    mpirun \
        -hostfile "${hostfile}" \
        -n 1 \
        -x PATH -x LD_LIBRARY_PATH \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
        bash -lc "$1"
}

reference_prefix="ddf_norm_collide_mode0_"
reference_checkpoint="${reference_prefix}00000064"

run_case \
    "export CUDA_VISIBLE_DEVICES=0; exec ./main3d.gnu.TPROF.MPI.CUDA.ex config/inputs \
max_step=64 amr.plot_int=1000000 checkpoint.chk_int=64 \
checkpoint.chk_prefix=${reference_prefix} checkpoint.keep_latest_only=0 \
checkpoint.write_particles=0 lbm.collide_mode=0"

run_case \
    "export CUDA_VISIBLE_DEVICES=0; exec ./main3d.gnu.TPROF.MPI.CUDA.ex config/inputs \
max_step=64 amr.plot_int=1000000 checkpoint.chk_int=-1 \
checkpoint.write_particles=0 lbm.collide_mode=1 \
verification.ddf_reference_checkpoint=${reference_checkpoint}"
