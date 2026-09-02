#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_osi_stage7_restart
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-osi-stage7-restart.log
#DSUB -e logs/submit/%J-osi-stage7-restart.log
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
INPUTS="${CASE_DIR}/config/inputs_osi"

if [[ ! -x "${APP_EXE}" ]]; then
    echo "ERROR: ${APP_EXE} is missing; compile BOX3D_OSI first." >&2
    exit 1
fi
if [[ -z "${CCS_ALLOC_FILE:-}" || ! -r "${CCS_ALLOC_FILE}" ]]; then
    echo "ERROR: CCS_ALLOC_FILE is unavailable; submit through dsub." >&2
    exit 1
fi

HOSTFILE="$(mktemp /tmp/box3d_osi_stage7_restart.XXXXXX)"
trap 'rm -f "${HOSTFILE}"' EXIT
awk '{ if (length($1) > 0 && length($2) > 0) print $1 " slots=" $2 }' \
    "${CCS_ALLOC_FILE}" > "${HOSTFILE}"

RUN_DIR="${CASE_DIR}/tmp/stage7_restart_${CCS_JOB_ID:-manual}"
mkdir -p "${RUN_DIR}/reference" "${RUN_DIR}/restart" \
    "${RUN_DIR}/ab_reference" "${RUN_DIR}/ab_restart"
cd "${RUN_DIR}"

run_case() {
    local ranks=$1
    shift
    mpirun \
        -hostfile "${HOSTFILE}" \
        -n "${ranks}" \
        -x PATH -x LD_LIBRARY_PATH \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
        bash -lc 'export CUDA_VISIBLE_DEVICES=0; exec "$@"' bash \
        "${APP_EXE}" "${INPUTS}" "$@"
}

echo "stage7_case: uninterrupted ranks=1 steps=16 checkpoint=reference/chk00000016"
run_case 1 \
    max_step=16 \
    amr.max_level=1 \
    'lbm.err=1.0e-12 1.0e-12' \
    verification.particle_checksum=true \
    amr.plot_int=8 \
    checkpoint.chk_int=16 \
    checkpoint.chk_prefix=reference/chk \
    checkpoint.keep_latest_only=false \
    checkpoint.write_particles=true

echo "stage7_case: checkpoint_source ranks=1 steps=8 checkpoint=restart/chk00000008"
run_case 1 \
    max_step=8 \
    amr.max_level=1 \
    'lbm.err=1.0e-12 1.0e-12' \
    verification.particle_checksum=true \
    checkpoint.chk_int=8 \
    checkpoint.chk_prefix=restart/chk \
    checkpoint.keep_latest_only=false \
    checkpoint.write_particles=true

echo "stage7_case: restarted ranks=2 begin_step=8 final_step=16"
run_case 2 \
    max_step=16 \
    amr.max_level=1 \
    'lbm.err=1.0e-12 1.0e-12' \
    verification.particle_checksum=true \
    checkpoint.begin_step=8 \
    checkpoint.chk_int=-1 \
    checkpoint.chk_prefix=restart/chk \
    checkpoint.write_particles=true \
    verification.ddf_reference_checkpoint=reference/chk00000016

echo "stage7_case: ab_uninterrupted ranks=1 steps=8 checkpoint=ab_reference/chk00000008"
run_case 1 \
    max_step=8 \
    lbm.stream_mode=0 \
    checkpoint.chk_int=8 \
    checkpoint.chk_prefix=ab_reference/chk \
    checkpoint.keep_latest_only=false \
    checkpoint.write_particles=false

echo "stage7_case: ab_checkpoint_source ranks=1 steps=4 checkpoint=ab_restart/chk00000004"
run_case 1 \
    max_step=4 \
    lbm.stream_mode=0 \
    checkpoint.chk_int=4 \
    checkpoint.chk_prefix=ab_restart/chk \
    checkpoint.keep_latest_only=false \
    checkpoint.write_particles=false

echo "stage7_case: ab_restarted ranks=2 begin_step=4 final_step=8"
run_case 2 \
    max_step=8 \
    lbm.stream_mode=0 \
    checkpoint.begin_step=4 \
    checkpoint.chk_int=-1 \
    checkpoint.chk_prefix=ab_restart/chk \
    checkpoint.write_particles=false \
    verification.ddf_reference_checkpoint=ab_reference/chk00000008

test -f reference/chk00000016/Header
test -f restart/chk00000008/Header
test -f reference/chk00000016/particles_0/Header
test -f restart/chk00000008/particles_0/Header
test -f plt000016/Header
grep -qx 'LBMCheckpointV2' reference/chk00000016/Header
grep -qx 'canonical_osi_single_array_v1' reference/chk00000016/Header
grep -qx 'canonical_ab_two_array_v1' ab_reference/chk00000008/Header
echo "stage7_artifacts: run_dir=${RUN_DIR} checkpoint_layout=canonical_osi_single_array_v1 ab_layout=canonical_ab_two_array_v1 plot=plt000016"
