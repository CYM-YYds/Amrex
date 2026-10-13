#!/usr/bin/env bash
set -euo pipefail
TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${TEST_DIR}/../../../../.." && pwd)"
TEST_BUILD_DIR="${SPARSE_INIT_BUILD_DIR:?指定独立测试构建目录}"
mkdir -p "${TEST_BUILD_DIR}"
cd "${TEST_BUILD_DIR}"
# 测试直接编译生产函数；fixture 只提供几何、参数和数据容器。
awk '
    /^amrex::BoxList MissingInterpolationSources\(/ { copying=1 }
    /^template <class T>/ { copying=0 }
    /^amrex::Interpolater\* AmrCoreLBM::DdfInterpolater\(/ { copying=1 }
    /^void AmrCoreLBM::/ {
        copying=($0 ~ /::BuildInterpolationStage\(/ ||
                 $0 ~ /::ApplyPhysicalBoundaryToInterpolationStage\(/ ||
                 $0 ~ /::InterpolateDdfStageToFine\(/ ||
                 $0 ~ /::ScaleInterpolationStage\(/ ||
                 $0 ~ /::InitializeDdfFromCoarse\(/)
    }
    copying { print }
' "${TEST_DIR}/../src/AmrCoreLBM_amr.cpp" > sparse_initialization_impl.inc
awk '
    /^void AmrCoreLBM::ParallelCopyOsi\(/ { copying=1 }
    /^void AmrCoreLBM::DecodeOsiValid\(/ { copying=0 }
    copying { print }
' "${TEST_DIR}/../src/AmrCoreLBM_osi.cpp" >> sparse_initialization_impl.inc
for name in DdfInterpolater BuildInterpolationStage ApplyPhysicalBoundaryToInterpolationStage ScaleInterpolationStage InterpolateDdfStageToFine InitializeDdfFromCoarse ParallelCopyOsi; do
    grep -Fq "AmrCoreLBM::${name}(" sparse_initialization_impl.inc
done
make -f "${TEST_DIR}/SparseInitialization.GNUmakefile" TEST_DIR="${TEST_DIR}" \
    AMREX_HOME="${REPO_DIR}/amrex-26.06" USE_CUDA="${USE_CUDA:-TRUE}" \
    CUDA_ARCH="${CUDA_ARCH:-80}" -j "${MAKE_J:-8}" > build.log 2>&1 || { tail -100 build.log; exit 1; }
