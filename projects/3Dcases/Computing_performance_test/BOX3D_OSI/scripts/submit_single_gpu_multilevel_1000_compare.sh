#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_single_gpu_multilevel_1000_compare
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-single-gpu-multilevel-1000.log
#DSUB -e logs/submit/%J-single-gpu-multilevel-1000.log
#DSUB -l cuda122

set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1
module load compilers/cuda/12.8.0
module load compilers/gcc/11.3.0

CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_EXE="${CASE_DIR}/main3d.gnu.TPROF.MPI.CUDA.ex"
INPUTS="${CASE_DIR}/config/inputs"
RUN_DIR="${CASE_DIR}/tmp/single_gpu_multilevel_1000_${CCS_JOB_ID:-manual}"
mkdir -p "${RUN_DIR}/seed" "${RUN_DIR}/ab" "${RUN_DIR}/osi"

[[ -x "${APP_EXE}" ]] || { echo "ERROR: executable missing" >&2; exit 1; }

run_case() {
    local label=$1
    local dir=$2
    shift 2
    echo "single_gpu_multilevel_1000_begin: mode=${label}"
    cd "${dir}"
    mpirun -n 1 -x PATH -x LD_LIBRARY_PATH \
        bash -lc 'export CUDA_VISIBLE_DEVICES=0; exec "$@"' bash \
        "${APP_EXE}" "${INPUTS}" \
        amr.max_level=2 amr.max_grid_size="128 128 128" \
        amr.blocking_factor_x="32 32 32" \
        amr.blocking_factor_y="32 32 32" \
        amr.blocking_factor_z="32 32 32" \
        amr.plot_int=-1 checkpoint.keep_latest_only=false \
        checkpoint.write_particles=false verification.convergence_enabled=false \
        verification.osi_ab_check=false verification.osi_seed_pattern=false \
        amr.regrid_int=32 "$@"
    echo "single_gpu_multilevel_1000_end: mode=${label}"
}

run_case SEED "${RUN_DIR}/seed" \
    max_step=96 performance.report_int=96 lbm.stream_mode=0 \
    checkpoint.begin_step=0 checkpoint.chk_int=96 checkpoint.chk_prefix=seed_chk

ln -s "${RUN_DIR}/seed/seed_chk00000096" "${RUN_DIR}/ab/ab_chk00000096"
run_case AB "${RUN_DIR}/ab" \
    max_step=1096 performance.report_int=1096 lbm.stream_mode=0 \
    checkpoint.begin_step=96 checkpoint.chk_int=-1 checkpoint.chk_prefix=ab_chk

ln -s "${RUN_DIR}/seed/seed_chk00000096" "${RUN_DIR}/osi/osi_chk00000096"
run_case OSI_PARALLELCOPY "${RUN_DIR}/osi" \
    max_step=1096 performance.report_int=1096 lbm.stream_mode=1 \
    lbm.osi_local_direct=1 lbm.osi_parallel_copy=1 lbm.osi_mpi_direct=0 \
    checkpoint.begin_step=96 checkpoint.chk_int=-1 checkpoint.chk_prefix=osi_chk

echo "single_gpu_multilevel_1000_artifacts: run_dir=${RUN_DIR}"
