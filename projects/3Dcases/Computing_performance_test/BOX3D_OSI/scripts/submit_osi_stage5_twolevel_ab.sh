#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_osi_stage5_twolevel_ab
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-osi-stage5-twolevel-ab.log
#DSUB -e logs/submit/%J-osi-stage5-twolevel-ab.log
#DSUB -l cuda122
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1
module load compilers/cuda/12.8.0
module load compilers/gcc/11.3.0
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export AVERAGE_MODE="${AVERAGE_MODE:-1}"
export REFINE_ERR="${REFINE_ERR:-0.0}"
export TEST_STEPS="${TEST_STEPS:-32}"
export MPI_RANKS="${MPI_RANKS:-2}"
if [[ "${AVERAGE_MODE}" != "0" && "${AVERAGE_MODE}" != "1" ]]; then
  echo "AVERAGE_MODE must be 0 or 1" >&2
  exit 2
fi
if ! [[ "${MPI_RANKS}" =~ ^[1-9][0-9]*$ ]]; then
  echo "MPI_RANKS must be a positive integer" >&2
  exit 2
fi
if [[ -z "${CCS_ALLOC_FILE:-}" || ! -r "${CCS_ALLOC_FILE}" ]]; then exit 1; fi
hostfile=$(mktemp /tmp/box3d_osi_stage5_ab.XXXXXX); trap 'rm -f "$hostfile"' EXIT
awk '{if (length($1)>0 && length($2)>0) print $1 " slots=" $2}' "$CCS_ALLOC_FILE" > "$hostfile"
mpirun -hostfile "$hostfile" -n "${MPI_RANKS}" -x PATH -x LD_LIBRARY_PATH -x AVERAGE_MODE -x REFINE_ERR -x TEST_STEPS \
  --mca plm_rsh_agent /opt/batch/agent/tools/dstart bash -lc \
  'export CUDA_VISIBLE_DEVICES=0; exec ./main3d.gnu.TPROF.MPI.CUDA.ex config/inputs_osi amr.max_level=1 amr.regrid_int=-1 max_step=${TEST_STEPS} stop_time=${TEST_STEPS} lbm.stream_mode=0 lbm.average_mode=${AVERAGE_MODE} lbm.cf_mask_mode=1 verification.osi_seed_pattern=true verification.partial_refine_box=true lbm.err=${REFINE_ERR} amr.plot_int=-1 checkpoint.chk_int=-1'
