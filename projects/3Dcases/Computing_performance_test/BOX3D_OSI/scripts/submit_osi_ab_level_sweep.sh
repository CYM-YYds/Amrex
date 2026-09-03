#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_osi_ab_levels
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-osi-ab-level-sweep.log
#DSUB -e logs/submit/%J-osi-ab-level-sweep.log
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
HOSTFILE="$(mktemp /tmp/box3d_osi_ab_levels.XXXXXX)"
trap 'rm -f "${HOSTFILE}"' EXIT
awk '{ if (length($1) > 0 && length($2) > 0) print $1 " slots=" $2 }' \
    "${CCS_ALLOC_FILE}" > "${HOSTFILE}"

RUN_ROOT="${CASE_DIR}/tmp/ab_level_sweep_${CCS_JOB_ID:-manual}"
mkdir -p "${RUN_ROOT}"

run_case() {
    local run_dir=$1
    shift
    mkdir -p "${run_dir}"
    cd "${run_dir}"
    mpirun -hostfile "${HOSTFILE}" -n 1 -x PATH -x LD_LIBRARY_PATH \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
        bash -lc 'export CUDA_VISIBLE_DEVICES=0; exec "$@"' bash \
        "${APP_EXE}" "${INPUTS}" amr.plot_int=-1 \
        checkpoint.keep_latest_only=false checkpoint.write_particles=false \
        verification.check_state_after_regrid=false "$@"
}

for max_level in 1 2; do
    seed_step=$((32 * max_level))
    final_step=$((seed_step + 1))
    case_root="${RUN_ROOT}/level${max_level}"
    seed_dir="${case_root}/seed"
    osi_dir="${case_root}/osi"

    run_case "${seed_dir}" max_step="${seed_step}" \
        amr.max_level="${max_level}" amr.regrid_int=32 lbm.stream_mode=0 \
        checkpoint.begin_step=0 checkpoint.chk_int="${seed_step}" \
        checkpoint.chk_prefix=chk

    run_case "${seed_dir}" max_step="${final_step}" \
        amr.max_level="${max_level}" amr.regrid_int=-1 lbm.stream_mode=0 \
        checkpoint.begin_step="${seed_step}" checkpoint.chk_int="${final_step}" \
        checkpoint.chk_prefix=chk

    run_case "${osi_dir}" max_step="${final_step}" \
        amr.max_level="${max_level}" amr.regrid_int=-1 lbm.stream_mode=1 \
        checkpoint.begin_step="${seed_step}" checkpoint.chk_int=-1 \
        checkpoint.chk_prefix="${seed_dir}/chk" \
        verification.ddf_reference_checkpoint="${seed_dir}/$(printf 'chk%08d' "${final_step}")"

    echo "ab_level_sweep_case_done: max_level=${max_level} seed_step=${seed_step}"
done

echo "ab_level_sweep_done: run_root=${RUN_ROOT}"
