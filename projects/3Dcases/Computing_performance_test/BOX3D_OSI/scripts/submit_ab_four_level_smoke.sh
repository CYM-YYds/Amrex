#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_ab_four_level_smoke
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-ab-four-level-smoke.log
#DSUB -e logs/submit/%J-ab-four-level-smoke.log
#DSUB -l cuda122

set -euo pipefail

MAX_STEP="${AB_MAX_STEP:-256}"
PLOT_INT="${AB_PLOT_INT:-128}"
CHK_INT="${AB_CHK_INT:-${MAX_STEP}}"

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

if [[ ! -x "${APP_EXE}" ]]; then
    echo "ERROR: ${APP_EXE} is missing; compile BOX3D_OSI first." >&2
    exit 1
fi
if [[ -z "${CCS_ALLOC_FILE:-}" || ! -r "${CCS_ALLOC_FILE}" ]]; then
    echo "ERROR: CCS_ALLOC_FILE is unavailable; submit through dsub." >&2
    exit 1
fi

HOSTFILE="$(mktemp /tmp/box3d_ab_four_level_smoke.XXXXXX)"
trap 'rm -f "${HOSTFILE}"' EXIT
awk '{ if (length($1) > 0 && length($2) > 0) print $1 " slots=" $2 }' \
    "${CCS_ALLOC_FILE}" > "${HOSTFILE}"

RUN_DIR="${CASE_DIR}/tmp/ab_four_level_smoke_${CCS_JOB_ID:-manual}"
mkdir -p "${RUN_DIR}"
cd "${RUN_DIR}"

mpirun \
    -hostfile "${HOSTFILE}" \
    -n 1 \
    -x PATH -x LD_LIBRARY_PATH \
    --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
    bash -lc 'export CUDA_VISIBLE_DEVICES=0; exec "$@"' bash \
    "${APP_EXE}" "${INPUTS}" \
    max_step="${MAX_STEP}" \
    amr.max_level=3 \
    amr.regrid_int=32 \
    amr.plot_int="${PLOT_INT}" \
    amr.plot_file=plt \
    checkpoint.chk_int="${CHK_INT}" \
    checkpoint.chk_prefix=chk \
    checkpoint.keep_latest_only=false \
    checkpoint.write_particles=false \
    lbm.stream_mode=0 \
    verification.check_state_after_regrid=true \
    verification.check_state_each_substep=true

if (( PLOT_INT > 0 )); then
    for step in $(seq "${PLOT_INT}" "${PLOT_INT}" "${MAX_STEP}" | awk '{printf "%08d\n", $1}'); do
        test -f "plt${step}/Header"
        test -f "pltdensity_${step}/Header"
        test -f "pltvort_${step}/Header"
    done
fi
if (( CHK_INT > 0 )); then
    CHK_TAG=$(printf '%08d' "${CHK_INT}")
    test -f "chk${CHK_TAG}/Header"
    grep -qx 'LBMCheckpointV2' "chk${CHK_TAG}/Header"
    grep -qx 'canonical_ab_two_array_v1' "chk${CHK_TAG}/Header"
fi
echo "ab_four_level_smoke_pass: run_dir=${RUN_DIR} plots=${PLOT_INT}..${MAX_STEP} checkpoint=${CHK_INT}"
