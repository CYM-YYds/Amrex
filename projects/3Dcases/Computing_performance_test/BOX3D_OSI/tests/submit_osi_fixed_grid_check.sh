#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_osi_stage3_mpi
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=2
#DSUB -N 1
#DSUB -o logs/submit/%J-osi-stage3-mpi.log
#DSUB -e logs/submit/%J-osi-stage3-mpi.log
#DSUB -l cuda122
set -euo pipefail

if [[ ${1:-} == --help ]]; then
    echo '用法：./tests/submit_osi_fixed_grid_check.sh [periodic|nonperiodic]（默认 periodic）'
    exit 0
fi
if (( $# > 1 )); then
    echo 'ERROR: 只接受一个边界预设：periodic 或 nonperiodic' >&2
    exit 2
fi
boundary=${1:-${BOX3D_FIXED_GRID_BOUNDARY:-periodic}}
case "${boundary}" in
    periodic)
        job_name=box3d_osi_stage3_mpi
        log_suffix=osi-stage3-mpi
        # 保留旧周期入口的数组参数拆分和兼容开关，便于完整 argv 回归。
        geometry_args=(geometry.is_periodic=1 1 1)
        direct_args=(lbm.osi_local_direct=1 lbm.osi_mpi_direct=1)
        ;;
    nonperiodic)
        job_name=box3d_osi_plan_nonperiodic
        log_suffix=osi-plan-nonperiodic
        geometry_args=('geometry.is_periodic=0 0 0')
        direct_args=()
        ;;
    *) echo 'ERROR: 边界预设必须为 periodic 或 nonperiodic' >&2; exit 2 ;;
esac

TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASE_DIR="$(cd "${TEST_DIR}/.." && pwd)"
cd "${CASE_DIR}"
if [[ -z ${CCS_JOB_ID:-} ]]; then
    export CCS_CLI_HOME=${CCS_CLI_HOME:-/opt/batch/cli}
    mkdir -p logs/submit
    exec dsub -n "${job_name}" \
        -o "logs/submit/%J-${log_suffix}.log" \
        -e "logs/submit/%J-${log_suffix}.log" \
        -x "all;BOX3D_FIXED_GRID_BOUNDARY=${boundary};" -s "${TEST_DIR}/submit_osi_fixed_grid_check.sh"
fi

source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1
module load compilers/cuda/12.8.0
module load compilers/gcc/11.3.0
source "${CASE_DIR}/scripts/osi_experiment_common.sh"
APP_EXE=./main3d.gnu.TPROF.MPI.CUDA.ex
[[ -x ${APP_EXE} ]] || { echo "ERROR: ${APP_EXE} is missing; compile BOX3D_OSI first." >&2; exit 1; }
[[ -r ${CCS_ALLOC_FILE:-} ]] || { echo 'ERROR: CCS_ALLOC_FILE is unavailable; submit through dsub.' >&2; exit 1; }
HOSTFILE="$(mktemp /tmp/box3d_osi_fixed_grid.XXXXXX)"
trap 'rm -f "${HOSTFILE}"' EXIT
osi_write_hostfile "${HOSTFILE}"
if [[ ${boundary} == periodic && ! -s ${HOSTFILE} ]]; then
    echo 'ERROR: no hosts found in CCS_ALLOC_FILE.' >&2
    exit 1
fi
osi_launch_mpi hmpi "${HOSTFILE}" 2 2 "${APP_EXE}" config/inputs \
    max_step=64 performance.report_int=64 \
    amr.max_level=0 amr.max_grid_size=64 \
    amr.blocking_factor_x=64 amr.blocking_factor_y=64 amr.blocking_factor_z=64 \
    "${geometry_args[@]}" amr.regrid_int=-1 amr.plot_int=-1 \
    checkpoint.chk_int=-1 verification.convergence_enabled=false \
    verification.osi_seed_pattern=true verification.osi_ab_check=true \
    lbm.stream_mode=1 "${direct_args[@]}" ${AMREX_RUN_ARGS:-}
