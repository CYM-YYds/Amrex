#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_osi_stage5_twolevel
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-osi-stage5-twolevel.log
#DSUB -e logs/submit/%J-osi-stage5-twolevel.log
#DSUB -l cuda122
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1
module load compilers/cuda/12.8.0
module load compilers/gcc/11.3.0
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -z "${CCS_ALLOC_FILE:-}" || ! -r "${CCS_ALLOC_FILE}" ]]; then exit 1; fi
hostfile=$(mktemp /tmp/box3d_osi_stage5.XXXXXX); trap 'rm -f "$hostfile"' EXIT
awk '{if (length($1)>0 && length($2)>0) print $1 " slots=" $2}' "$CCS_ALLOC_FILE" > "$hostfile"
mpirun -hostfile "$hostfile" -n 1 -x PATH -x LD_LIBRARY_PATH \
  --mca plm_rsh_agent /opt/batch/agent/tools/dstart bash -lc \
  'export CUDA_VISIBLE_DEVICES=0; exec ./main3d.gnu.TPROF.MPI.CUDA.ex config/inputs_osi amr.max_level=1 amr.regrid_int=1 max_step=4 stop_time=4 verification.osi_ab_check=false'
