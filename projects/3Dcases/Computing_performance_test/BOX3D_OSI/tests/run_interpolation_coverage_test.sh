#!/usr/bin/env bash
set -euo pipefail

TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${TEST_DIR}/../../../../.." && pwd)"
TEST_BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/box3d-interpolation-coverage.XXXXXX")"
trap 'rm -rf "${TEST_BUILD_DIR}"' EXIT

cd "${TEST_BUILD_DIR}"
make -f "${TEST_DIR}/InterpolationCoverage.GNUmakefile" \
    TEST_DIR="${TEST_DIR}" AMREX_HOME="${REPO_DIR}/amrex-26.06" \
    -j "${MAKE_J:-8}" > build.log 2>&1 || { cat build.log; exit 1; }
./interpolation_coverage3d.gnu.ex > pass.log 2>&1
cat pass.log

# 负例必须由覆盖检查终止，并包含可操作的建议和定位信息。
if ./interpolation_coverage3d.gnu.ex coverage_fail=1 > fail.log 2>&1; then
    echo "ERROR: missing coverage did not abort" >&2
    exit 1
fi
for expected in '[InterpolationCoverage]' 'fine_level=2' 'fine_fab=7' \
    'missing_coarse_boxes=' 'amr.n_proper' 'proper nesting' 'FillPatchNLevels'; do
    if ! grep -Fq "${expected}" fail.log; then
        cat fail.log
        echo "ERROR: missing diagnostic: ${expected}" >&2
        exit 1
    fi
done
echo 'PASS missing coverage aborts with location and repair suggestions'
