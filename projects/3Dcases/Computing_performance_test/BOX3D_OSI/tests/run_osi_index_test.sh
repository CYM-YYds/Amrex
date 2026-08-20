#!/usr/bin/env bash
set -euo pipefail

TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/box3d-osi-index.XXXXXX")"
trap 'rm -rf "${TEST_BUILD_DIR}"' EXIT

if [[ -n "${CXX:-}" ]]; then
    TEST_CXX="${CXX}"
elif [[ -x /home/HPCBase/compilers/gcc/11.3.0/bin/g++ ]]; then
    TEST_CXX=/home/HPCBase/compilers/gcc/11.3.0/bin/g++
else
    TEST_CXX=c++
fi

"${TEST_CXX}" \
    -std=c++20 \
    -O2 \
    -Wall \
    -Wextra \
    -Wpedantic \
    -Werror \
    "${TEST_DIR}/osi_index_test.cpp" \
    -o "${TEST_BUILD_DIR}/osi_index_test"

"${TEST_BUILD_DIR}/osi_index_test"
