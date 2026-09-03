#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_osi_validation_smoke
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-osi-validation-smoke.log
#DSUB -e logs/submit/%J-osi-validation-smoke.log
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
APP_EXE="${CASE_DIR}/main3d.gnu.TPROF.MPI.CUDA.ex"
INPUTS="${CASE_DIR}/config/inputs"
HOSTFILE="$(mktemp /tmp/box3d_osi_validation_smoke.XXXXXX)"
trap 'rm -f "${HOSTFILE}"' EXIT
awk '{ if (length($1) > 0 && length($2) > 0) print $1 " slots=" $2 }' \
    "${CCS_ALLOC_FILE}" > "${HOSTFILE}"

RUN_DIR="${CASE_DIR}/tmp/validation_smoke_${CCS_JOB_ID:-manual}"
mkdir -p "${RUN_DIR}"
cd "${RUN_DIR}"

mpirun -hostfile "${HOSTFILE}" -n 1 -x PATH -x LD_LIBRARY_PATH \
    --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
    bash -lc 'export CUDA_VISIBLE_DEVICES=0; exec "$@"' bash \
    "${APP_EXE}" "${INPUTS}" \
    max_step=64 amr.max_level=1 amr.regrid_int=32 \
    amr.plot_int=32 amr.plot_file=plt \
    checkpoint.chk_int=64 checkpoint.chk_prefix=chk \
    checkpoint.keep_latest_only=false checkpoint.write_particles=false \
    verification.check_state_after_regrid=true \
    verification.check_state_each_substep=true

for step in 000032 000064; do
    test -f "plt${step}/Header"
    test -f "pltdensity_${step}/Header"
    test -f "pltvort_${step}/Header"
done
test -f chk00000064/Header
grep -qx 'LBMCheckpointV2' chk00000064/Header
grep -qx 'canonical_osi_single_array_v1' chk00000064/Header
echo "validation_smoke_pass: run_dir=${RUN_DIR} plots=32,64 checkpoint=64"
