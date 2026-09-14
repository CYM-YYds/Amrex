#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_osi_device_perf
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=16;mem=49152;gpu=2
#DSUB -N 1
#DSUB -o logs/submit/%J-osi-mpi-device-perf.log
#DSUB -e logs/submit/%J-osi-mpi-device-perf.log
#DSUB -l cuda122

set -euo pipefail

source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/openmpi/4.1.5_cuda11.6
module load compilers/cuda/12.8.0
module load compilers/gcc/11.3.0

# 使用平台提供的 CUDA-aware OpenMPI；显式前缀用于修正模块中的可重定位配置。
export OPAL_PREFIX=/home/HPCBase/mpi/openmpi/4.1.5_cuda11.6

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
APP_EXE="${CASE_DIR}/main3d_cuda_aware3d.gnu.TPROF.MPI.CUDA.ex"
HOSTFILE="$(mktemp /tmp/box3d_osi_device_perf.XXXXXX)"
trap 'rm -f "${HOSTFILE}"' EXIT
awk '{ if (length($1) > 0 && length($2) > 0) print $1 " slots=" $2 }' \
    "${CCS_ALLOC_FILE}" > "${HOSTFILE}"

run_case() {
    local label=$1
    shift
    echo "mpi_device_perf_begin: mode=${label} ranks=2 steps=1000 grids=8 periodic=1"
    mpirun \
        -hostfile "${HOSTFILE}" -n 2 -npernode 2 \
        -x PATH -x LD_LIBRARY_PATH -x OPAL_PREFIX \
        --mca pml ucx \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
        bash -lc 'export CUDA_VISIBLE_DEVICES=${OMPI_COMM_WORLD_LOCAL_RANK:-${MPI_LOCALRANKID:-0}}; exec "$@"' bash \
        "${APP_EXE}" config/inputs \
        max_step=1000 performance.report_int=1000 \
        amr.max_level=0 amr.max_grid_size=64 \
        amr.blocking_factor_x=64 amr.blocking_factor_y=64 amr.blocking_factor_z=64 \
        'geometry.is_periodic=1 1 1' amr.regrid_int=-1 amr.plot_int=-1 \
        checkpoint.chk_int=-1 verification.convergence_enabled=false \
        verification.osi_seed_pattern=false verification.osi_ab_check=false \
        "$@"
    echo "mpi_device_perf_end: mode=${label}"
}

cd "${CASE_DIR}"
run_case AB \
    lbm.stream_mode=0 lbm.osi_local_direct=0 lbm.osi_mpi_direct=0 \
    lbm.osi_mpi_device_direct=0
run_case OSI_HOST_OVERLAP \
    lbm.stream_mode=1 lbm.osi_local_direct=1 lbm.osi_mpi_direct=1 \
    lbm.osi_mpi_device_direct=0
run_case OSI_DEVICE_OVERLAP \
    lbm.stream_mode=1 lbm.osi_local_direct=1 lbm.osi_mpi_direct=1 \
    lbm.osi_mpi_device_direct=1 amrex.use_gpu_aware_mpi=1
