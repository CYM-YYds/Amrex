#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_osi_stage6_regrid
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-osi-stage6-regrid.log
#DSUB -e logs/submit/%J-osi-stage6-regrid.log
#DSUB -l cuda122
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1
module load compilers/cuda/12.8.0
module load compilers/gcc/11.3.0
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

export TEST_STEPS="${TEST_STEPS:-10}"
export REGRID_INT="${REGRID_INT:-2}"
export MPI_RANKS="${MPI_RANKS:-2}"
if ! [[ "${TEST_STEPS}" =~ ^[1-9][0-9]*$ &&
        "${REGRID_INT}" =~ ^[1-9][0-9]*$ &&
        "${MPI_RANKS}" =~ ^[1-9][0-9]*$ ]]; then
  echo "TEST_STEPS, REGRID_INT and MPI_RANKS must be positive integers" >&2
  exit 2
fi
if [[ -z "${CCS_ALLOC_FILE:-}" || ! -r "${CCS_ALLOC_FILE}" ]]; then exit 1; fi
hostfile=$(mktemp /tmp/box3d_osi_stage6.XXXXXX)
trap 'rm -f "$hostfile"' EXIT
awk '{if (length($1)>0 && length($2)>0) print $1 " slots=" $2}' \
  "$CCS_ALLOC_FILE" > "$hostfile"
mpirun -hostfile "$hostfile" -n "${MPI_RANKS}" \
  -x PATH -x LD_LIBRARY_PATH -x TEST_STEPS -x REGRID_INT \
  --mca plm_rsh_agent /opt/batch/agent/tools/dstart bash -lc \
  'export CUDA_VISIBLE_DEVICES=0; exec ./main3d.gnu.TPROF.MPI.CUDA.ex config/inputs_osi amr.max_level=1 amr.regrid_int=${REGRID_INT} max_step=${TEST_STEPS} stop_time=${TEST_STEPS} verification.osi_ab_check=false verification.osi_seed_pattern=true verification.dynamic_refine_box=true verification.partial_refine_box=false lbm.stream_mode=1 lbm.collide_mode=1 lbm.interp_mode=0 lbm.average_mode=1 lbm.cf_mask_mode=1 amr.plot_int=-1 checkpoint.chk_int=-1'
