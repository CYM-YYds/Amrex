#!/usr/bin/env bash
# 直接以 CPU 和地址检查器测试生产头文件，不需要 GPU 或链接 AMReX 运行时。
set -euo pipefail
TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(git -C "$TEST_DIR" rev-parse --show-toplevel)"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/lbm-dimension-kernels.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT
TEST_CXX="${CXX:-/home/HPCBase/compilers/gcc/11.3.0/bin/g++}"
for precision in ${PRECISION:-DOUBLE FLOAT}; do
    [[ "$precision" == DOUBLE || "$precision" == FLOAT ]] || { echo "错误: PRECISION 必须为 DOUBLE 或 FLOAT" >&2; exit 1; }
for dimension in 2 3; do
    # 最小 CPU 配置只用于这些无运行时依赖的内核；维度来自同一个 AMReX 宏。
    printf '#define AMREX_SPACEDIM %s\n#define BL_SPACEDIM %s\n#define AMREX_Linux 1\n#define NDEBUG 1\n#define AMREX_GPU_MAX_THREADS 256\n' \
        "$dimension" "$dimension" > "$BUILD_DIR/AMReX_Config.H"
    if [[ "$precision" == FLOAT ]]; then printf '#define BL_USE_FLOAT 1\n#define AMREX_USE_FLOAT 1\n' >> "$BUILD_DIR/AMReX_Config.H"; fi
    echo "Testing DIM=$dimension PRECISION=$precision"
    "$TEST_CXX" -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
        -Wno-attributes -Wno-psabi -I"$BUILD_DIR" -I"$TEST_DIR/../src" \
        -I"$REPO_DIR/amrex-26.06/Src/Base" -I"$REPO_DIR/amrex-26.06/Src/Boundary" \
        "$TEST_DIR/dimension_kernels_test.cpp" -o "$BUILD_DIR/test${dimension}d"
    TEST_LIB_DIR="$(dirname "$("$TEST_CXX" -print-file-name=libasan.so)")"
    LD_PRELOAD="$("$TEST_CXX" -print-file-name=libasan.so)${LD_PRELOAD:+:$LD_PRELOAD}" \
        LD_LIBRARY_PATH="${TEST_LIB_DIR}:${LD_LIBRARY_PATH:-}" "$BUILD_DIR/test${dimension}d"
done
done
