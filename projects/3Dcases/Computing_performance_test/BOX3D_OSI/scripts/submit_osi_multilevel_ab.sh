#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_osi_multilevel_ab
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-osi-multilevel-ab.log
#DSUB -e logs/submit/%J-osi-multilevel-ab.log
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
HOSTFILE="$(mktemp /tmp/box3d_osi_multilevel_ab.XXXXXX)"
trap 'rm -f "${HOSTFILE}"' EXIT
awk '{ if (length($1) > 0 && length($2) > 0) print $1 " slots=" $2 }' \
    "${CCS_ALLOC_FILE}" > "${HOSTFILE}"

RUN_DIR="${CASE_DIR}/tmp/multilevel_ab_${CCS_JOB_ID:-manual}"
mkdir -p "${RUN_DIR}/seed" "${RUN_DIR}/osi"

run_case() {
    local run_dir=$1
    shift
    cd "${run_dir}"
    mpirun -hostfile "${HOSTFILE}" -n 1 -x PATH -x LD_LIBRARY_PATH \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
        bash -lc 'export CUDA_VISIBLE_DEVICES=0; exec "$@"' bash \
        "${APP_EXE}" "${INPUTS}" \
        amr.max_level=3 amr.plot_int=-1 \
        checkpoint.keep_latest_only=false checkpoint.write_particles=false \
        verification.check_state_after_regrid=false "$@"
}

# Build one canonical four-level state, then branch both storage modes from
# exactly the same hierarchy and DDF values.  This avoids adaptive tagging
# differences making the final BoxArrays incomparable.
run_case "${RUN_DIR}/seed" \
    max_step=96 amr.regrid_int=32 lbm.stream_mode=0 \
    checkpoint.begin_step=0 checkpoint.chk_int=96 checkpoint.chk_prefix=chk

run_case "${RUN_DIR}/seed" \
    max_step=97 amr.regrid_int=-1 lbm.stream_mode=0 \
    checkpoint.begin_step=96 checkpoint.chk_int=97 checkpoint.chk_prefix=chk

run_case "${RUN_DIR}/osi" \
    max_step=97 amr.regrid_int=-1 lbm.stream_mode=1 \
    checkpoint.begin_step=96 checkpoint.chk_int=-1 \
    checkpoint.chk_prefix="${RUN_DIR}/seed/chk" \
    verification.ddf_reference_checkpoint="${RUN_DIR}/seed/chk00000097"

MAX_ACTIVE_LINF="$(awk '
    /ddf_cell_norm_level:/ {
        for (i = 1; i <= NF; ++i) {
            if ($i ~ /^active_linf=/) {
                split($i, a, "=")
                if (a[2] + 0 > max) max = a[2] + 0
            }
        }
    }
    END { printf "%.17g", max + 0 }
' "${CASE_DIR}/logs/submit/${CCS_JOB_ID}-osi-multilevel-ab.log")"
awk -v value="${MAX_ACTIVE_LINF}" 'BEGIN { exit !(value <= 1.0e-12) }' || {
    echo "ERROR: four-level OSI/A-B active Linf ${MAX_ACTIVE_LINF} exceeds 1e-12" >&2
    exit 1
}

echo "multilevel_ab_artifacts: run_dir=${RUN_DIR}"
echo "multilevel_ab_pass: max_active_linf=${MAX_ACTIVE_LINF} tolerance=1e-12"
