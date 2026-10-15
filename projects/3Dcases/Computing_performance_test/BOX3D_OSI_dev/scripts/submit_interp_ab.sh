#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_interp_ab
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-interp-ab.log
#DSUB -e logs/submit/%J-interp-ab.log
#DSUB -l cuda122

set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1
module load compilers/cuda/12.8.0
module load compilers/gcc/12.2.0

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/osi_experiment_common.sh"
case_dir="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${case_dir}"

hostfile="$(mktemp /tmp/box3d-interp-ab.XXXXXX)"
trap 'rm -f "${hostfile}"' EXIT
osi_write_hostfile "${hostfile}"

for mode in 0 1 2; do
    echo "=== INTERP_AB_BEGIN mode=${mode} ==="
    osi_launch_mpi hmpi "${hostfile}" 1 1 \
        "${case_dir}/main3d.gnu.TPROF.MPI.CUDA.ex" "${case_dir}/config/inputs" \
        max_step=10000 lbm.interp_mode="${mode}" amr.regrid_int=1000000 \
        amr.plot_int=1000000 checkpoint.begin_step=9000 \
        checkpoint.chk_prefix=chk checkpoint.chk_int=-1
    echo "=== INTERP_AB_END mode=${mode} ==="
done
