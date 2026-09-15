#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_re1000_128k_ab_osi
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-re1000-single-gpu-128k-ab-osi.log
#DSUB -e logs/submit/%J-re1000-single-gpu-128k-ab-osi.log
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
HOSTFILE="$(mktemp /tmp/box3d_re1000_128k_ab_osi.XXXXXX)"
trap 'rm -f "${HOSTFILE}"' EXIT

if [[ ! -x "${APP_EXE}" ]]; then
    echo "ERROR: ${APP_EXE} is missing; compile BOX3D_OSI first." >&2
    exit 1
fi
if [[ -z "${CCS_ALLOC_FILE:-}" || ! -r "${CCS_ALLOC_FILE}" ]]; then
    echo "ERROR: CCS_ALLOC_FILE is unavailable; submit through dsub." >&2
    exit 1
fi

awk '{ if (length($1) > 0 && length($2) > 0) print $1 " slots=" $2 }' \
    "${CCS_ALLOC_FILE}" > "${HOSTFILE}"

RUN_DIR="${CASE_DIR}/tmp/re1000_single_gpu_128k_ab_osi_${CCS_JOB_ID:-manual}"
mkdir -p "${RUN_DIR}/seed" "${RUN_DIR}/ab" "${RUN_DIR}/osi"

run_case() {
    local label=$1
    local stream_mode=$2
    local run_dir=$3
    local begin_step=$4
    local max_step=$5
    local chk_prefix=$6

    echo "re1000_128k_begin: mode=${label}"
    cd "${run_dir}"
    mpirun \
        -hostfile "${HOSTFILE}" \
        -n 1 \
        -x PATH -x LD_LIBRARY_PATH \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
        bash -lc 'export CUDA_VISIBLE_DEVICES=0; exec "$@"' bash \
        "${APP_EXE}" "${INPUTS}" \
        max_step="${max_step}" \
        lbm.reynolds_number=1000.0 \
        lbm.stream_mode="${stream_mode}" \
        amr.max_level=2 \
        amr.regrid_int=32 \
        amr.plot_int=-1 \
        performance.report_int=3200 \
        checkpoint.begin_step="${begin_step}" \
        checkpoint.chk_int="$([[ "${label}" == SEED ]] && echo 3200 || echo 32000)" \
        checkpoint.chk_prefix="${chk_prefix}" \
        checkpoint.keep_latest_only=true \
        checkpoint.write_particles=false \
        verification.convergence_enabled=false \
        verification.osi_seed_pattern=false \
        verification.osi_ab_check=false
    echo "re1000_128k_end: mode=${label}"
}

# 初始大范围细化会使 A-B 在重网格时超出单卡显存。先由 OSI 生成第3200步
# canonical 公共检查点，再从相同的成熟流场和网格开始正式对照。
run_case SEED 1 "${RUN_DIR}/seed" 0 3200 seed_chk

ln -s "${RUN_DIR}/seed/seed_chk00003200" "${RUN_DIR}/ab/ab_chk00003200"
ln -s "${RUN_DIR}/seed/seed_chk00003200" "${RUN_DIR}/osi/osi_chk00003200"

# 两种模式在同一节点、同一 GPU 上顺序执行，避免并行争用和节点差异。
run_case AB 0 "${RUN_DIR}/ab" 3200 128000 ab_chk
run_case OSI 1 "${RUN_DIR}/osi" 3200 128000 osi_chk

echo "re1000_128k_artifacts: run_dir=${RUN_DIR}"
