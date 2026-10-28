#!/usr/bin/env bash
# 本次精度切换的临时数值检查；复现材料保留，任务结束后可按需删除。
set -euo pipefail
case_dir="$(cd "$(dirname "$0")/../../.." && pwd)"
repo_dir="$(git -C "$case_dir" rev-parse --show-toplevel)"
run_dir="$case_dir/runs/temporary/single_precision_particles"
for precision in FLOAT DOUBLE; do
    build_dir="$run_dir/cpu_$precision"
    mkdir -p "$build_dir"
    cat > "$build_dir/AMReX_Config.H" <<CONFIG
#define AMREX_SPACEDIM 3
#define BL_SPACEDIM 3
#define AMREX_Linux 1
#define NDEBUG 1
#define AMREX_GPU_MAX_THREADS 256
#define AMREX_SINGLE_PRECISION_PARTICLES 1
CONFIG
    if [[ "$precision" == FLOAT ]]; then
        printf '#define BL_USE_FLOAT 1\n#define AMREX_USE_FLOAT 1\n' >> "$build_dir/AMReX_Config.H"
    fi
    /home/HPCBase/compilers/gcc/11.3.0/bin/g++ -std=c++20 -O1 -Wno-attributes -Wno-psabi \
        -I"$build_dir" -I"$case_dir/src" -I"$repo_dir/amrex-26.06/Src/Base" \
        -I"$repo_dir/amrex-26.06/Src/Boundary" "$(dirname "$0")/ibm_check.cpp" -o "$build_dir/check"
    "$build_dir/check"
done
