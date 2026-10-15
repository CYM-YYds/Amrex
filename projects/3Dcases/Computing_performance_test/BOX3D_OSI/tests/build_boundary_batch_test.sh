#!/usr/bin/env bash
set -euo pipefail
TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${TEST_DIR}/../../../../.." && pwd)"
TEST_BUILD_DIR="${BOUNDARY_BATCH_BUILD_DIR:?指定独立测试构建目录}"
mkdir -p "${TEST_BUILD_DIR}"
cd "${TEST_BUILD_DIR}"
# 直接提取生产类型、几何构建和执行函数；fixture 仅提供所需数据容器。
awk '
    /^struct BoundaryWorkTag/ { copying=1 }
    /^class ScopedPerfTimer/ { copying=0 }
    copying { print }
' "${TEST_DIR}/../src/AmrCoreLBM.H" > boundary_batch_types.inc
awk '
    /^void AmrCoreLBM::BuildBoundaryWorkBoxes\(/ { copying=1 }
    /^void AmrCoreLBM::BuildInterpolationCache\(/ { copying=0 }
    copying { print }
' "${TEST_DIR}/../src/AmrCoreLBM_amr.cpp" > boundary_batch_impl.inc
awk '
    /^void AmrCoreLBM::ApplyPhysicalBoundaryLevel\(/ { copying=1 }
    /^void AmrCoreLBM::Boundary\(/ { copying=0 }
    copying { print }
' "${TEST_DIR}/../src/AmrCoreLBM_advance.cpp" >> boundary_batch_impl.inc
for name in BuildBoundaryWorkBoxes ApplyPhysicalBoundaryLevel; do
    grep -Fq "AmrCoreLBM::${name}(" boundary_batch_impl.inc
done
make -f "${TEST_DIR}/BoundaryBatch.GNUmakefile" TEST_DIR="${TEST_DIR}" \
    AMREX_HOME="${REPO_DIR}/amrex-26.06" USE_CUDA="${USE_CUDA:-TRUE}" \
    CUDA_ARCH="${CUDA_ARCH:-80}" -j "${MAKE_J:-8}" > build.log 2>&1 || { tail -80 build.log; exit 1; }
