#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_osi_device_correctness
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=16;mem=49152;gpu=2
#DSUB -N 1
#DSUB -o logs/submit/%J-osi-mpi-device-correctness.log
#DSUB -e logs/submit/%J-osi-mpi-device-correctness.log
#DSUB -l cuda122

set -euo pipefail

source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/openmpi/4.1.5_cuda11.6
module load compilers/cuda/12.8.0
module load compilers/gcc/11.3.0

# 该 OpenMPI 模块可重定位，但 wrapper 内保留了原安装前缀；显式设置后才能在计算节点
# 正确找到运行时配置。该版本由 ompi_info 确认为 CUDA-aware 构建。
export OPAL_PREFIX=/home/HPCBase/mpi/openmpi/4.1.5_cuda11.6
# UCX 模块会强制加载 CUDA 11.8，与本算例的 CUDA 12.8 冲突；这里只加入其运行库。
UCX_ROOT=/home/HPCBase/apps/ucx_1.12.1_cuda11.8
export LD_LIBRARY_PATH="${UCX_ROOT}/lib:${UCX_ROOT}/lib/ucx:${LD_LIBRARY_PATH:-}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
APP_EXE="${CASE_DIR}/main3d_cuda_aware3d.gnu.TPROF.MPI.CUDA.ex"
HOSTFILE="$(mktemp /tmp/box3d_osi_device_correctness.XXXXXX)"
trap 'rm -f "${HOSTFILE}"' EXIT
awk '{ if (length($1) > 0 && length($2) > 0) print $1 " slots=" $2 }' \
    "${CCS_ALLOC_FILE}" > "${HOSTFILE}"

cd "${CASE_DIR}"
mpirun \
    -hostfile "${HOSTFILE}" -n 2 -npernode 2 \
    -x PATH -x LD_LIBRARY_PATH -x OPAL_PREFIX \
    --mca pml ucx \
    --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
    bash -lc 'export CUDA_VISIBLE_DEVICES=${OMPI_COMM_WORLD_LOCAL_RANK:-${MPI_LOCALRANKID:-0}}; exec "$@"' bash \
    "${APP_EXE}" config/inputs \
    max_step=64 performance.report_int=64 \
    amr.max_level=0 amr.max_grid_size=64 \
    amr.blocking_factor_x=64 amr.blocking_factor_y=64 amr.blocking_factor_z=64 \
    'geometry.is_periodic=1 1 1' amr.regrid_int=-1 amr.plot_int=-1 \
    checkpoint.chk_int=-1 verification.convergence_enabled=false \
    verification.osi_seed_pattern=true verification.osi_ab_check=true \
    lbm.stream_mode=1 lbm.osi_local_direct=1 lbm.osi_mpi_direct=1 \
    lbm.osi_mpi_device_direct=1 amrex.use_gpu_aware_mpi=1
