#!/bin/bash
# 使用独立目录构建 CUDA-aware OpenMPI 版本，不覆盖现有 HMPI 基线。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_HOST="${BUILD_HOST:-whshare-agent-1}"
MPI_ROOT=/home/HPCBase/mpi/openmpi/4.1.5_cuda11.6
GCC_ROOT=/home/HPCBase/compilers/gcc/11.3.0

run_build() {
    source /home/HPCBase/tools/module-5.2.0/init/profile.sh
    module use /home/HPCBase/modulefiles/
    module purge
    module load mpi/openmpi/4.1.5_cuda11.6
    module load compilers/cuda/12.8.0
    module load compilers/gcc/11.3.0
    export OPAL_PREFIX="${MPI_ROOT}"
    export OMPI_CC="${GCC_ROOT}/bin/gcc"
    export OMPI_CXX="${GCC_ROOT}/bin/g++"
    cd "${CASE_DIR}"
    make -j"${MAKE_J:-16}" TMP_BUILD_DIR=tmp_build_dir/cuda_aware \
        EBASE=main3d_cuda_aware CUDA_ARCH="${CUDA_ARCH:-80}"
}

if [[ "$(hostname)" == "${BUILD_HOST}" ]]; then
    run_build
else
    # 共享文件系统允许在固定编译节点直接使用同一算例路径。
    ssh "${BUILD_HOST}" "set -euo pipefail; \
        source /home/HPCBase/tools/module-5.2.0/init/profile.sh; \
        module use /home/HPCBase/modulefiles/; module purge; \
        module load mpi/openmpi/4.1.5_cuda11.6; \
        module load compilers/cuda/12.8.0; \
        module load compilers/gcc/11.3.0; \
        export OPAL_PREFIX=$(printf '%q' "${MPI_ROOT}"); \
        export OMPI_CC=$(printf '%q' "${GCC_ROOT}/bin/gcc"); \
        export OMPI_CXX=$(printf '%q' "${GCC_ROOT}/bin/g++"); \
        cd $(printf '%q' "${CASE_DIR}"); \
        make -j$(printf '%q' "${MAKE_J:-16}") \
            TMP_BUILD_DIR=tmp_build_dir/cuda_aware EBASE=main3d_cuda_aware \
            CUDA_ARCH=$(printf '%q' "${CUDA_ARCH:-80}")"
fi
