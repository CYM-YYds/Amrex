#!/usr/bin/env bash
set -euo pipefail
TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${TEST_DIR}/../../../../.." && pwd)"
TEST_BUILD_DIR="${SPARSE_INIT_BUILD_DIR:?指定独立测试构建目录}"
mkdir -p "${TEST_BUILD_DIR}"
cd "${TEST_BUILD_DIR}"
# 任务类型与数值/提交函数均从当前生产文件提取，不复制算法。
awk '
    /^struct InterpolationBatchTag/ { copying=1 }
    /^struct InterpolationLevelCache/ { copying=0 }
    copying { print }
' "${TEST_DIR}/../src/AmrCoreLBM.H" > sparse_initialization_types.inc
awk '
    /^amrex::BoxList MissingInterpolationSources\(/ { copying=1 }
    /^template <class T>/ { copying=0 }
    /^template <bool UseOsiAddress>/ { pending=$0; copying=0; next }
    /^amrex::Interpolater\* AmrCoreLBM::DdfInterpolater\(/ { copying=1 }
    /^void AmrCoreLBM::/ {
        copying=($0 ~ /::BuildInterpolationStage\(/ ||
                 $0 ~ /::ApplyPhysicalBoundaryToInterpolationStage\(/ ||
                 $0 ~ /::BuildInterpolationWork\(/ ||
                 $0 ~ /::InterpolateDdfStageBatch\(/ ||
                 $0 ~ /::InitializeDdfFromCoarse\(/)
        if (copying && $0 ~ /::InterpolateDdfStageBatch\(/) print pending
        pending=""
    }
    copying { print }
' "${TEST_DIR}/../src/AmrCoreLBM_amr.cpp" > sparse_initialization_impl.inc
awk '
    /^void AmrCoreLBM::/ {
        copying=0
        if ($0 ~ /::ParallelCopyOsi\(/) {
            heading=$0; getline
            if ($0 ~ /^    int lev,/) { copying=1; print heading; print; next }
        }
    }
    copying { print }
' "${TEST_DIR}/../src/AmrCoreLBM_osi.cpp" >> sparse_initialization_impl.inc
for name in DdfInterpolater BuildInterpolationStage ApplyPhysicalBoundaryToInterpolationStage BuildInterpolationWork InterpolateDdfStageBatch InitializeDdfFromCoarse ParallelCopyOsi; do
    grep -Fq "AmrCoreLBM::${name}(" sparse_initialization_impl.inc
done
make -f "${TEST_DIR}/SparseInitialization.GNUmakefile" TEST_DIR="${TEST_DIR}" \
    AMREX_HOME="${REPO_DIR}/amrex-26.06" USE_CUDA="${USE_CUDA:-TRUE}" \
    CUDA_ARCH="${CUDA_ARCH:-80}" -j "${MAKE_J:-8}" > build.log 2>&1 || { tail -100 build.log; exit 1; }
