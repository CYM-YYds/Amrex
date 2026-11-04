#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n C_macro_math_probe
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=8;mem=16384;gpu=1
#DSUB -N 1
#DSUB -l cuda122
# 本次审查临时GPU算术模型，保留源码、编译命令和结果以供复现。
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load compilers/cuda/12.8.0 compilers/gcc/11.3.0
CASE=$(cd "$(dirname "$0")/../../.." && pwd)
OUT="$CASE/runs/temporary/C_AB_OSI_stage_review/math_job${CCS_JOB_ID:?}"
mkdir -p "$OUT"
cp "$0" "$OUT/submit.sh"
cp "$(dirname "$0")/macro_math_probe.cu" "$OUT/"
hostname > "$OUT/node.txt"
nvidia-smi > "$OUT/gpu.txt"
for mode in fast precise; do
    args=(-O3 -arch=sm_80)
    if [[ "$mode" == fast ]]; then args+=(--use_fast_math); fi
    printf '%q ' nvcc "${args[@]}" "$OUT/macro_math_probe.cu" -o "$OUT/$mode.ex" > "$OUT/${mode}_command.txt"
    nvcc "${args[@]}" "$OUT/macro_math_probe.cu" -o "$OUT/$mode.ex" > "$OUT/${mode}_compile.log" 2>&1
    "$OUT/$mode.ex" > "$OUT/${mode}.json"
done

if [[ "${MACRO_PRODUCTION_PROBE:-0}" == 1 ]]; then
    cp "$(dirname "$0")/production_macro_probe.cu" "$(dirname "$0")/frozen_macros.inc" "$OUT/"
    SNAPSHOT="$CASE/runs/archive/history_20261101/a5_threshold_128000/frozen_20261029/src"
    cp "$SNAPSHOT/LbmLattice.H" "$SNAPSHOT/OsiIndex.H" "$OUT/"
    printf '#define AMREX_SPACEDIM 3\n#define BL_SPACEDIM 3\n#define AMREX_USE_FLOAT 1\n#define BL_USE_FLOAT 1\n#define AMREX_USE_GPU 1\n#define AMREX_USE_CUDA 1\n#define AMREX_GPU_MAX_THREADS 256\n#define AMREX_Linux 1\n#define NDEBUG 1\n' > "$OUT/AMReX_Config.H"
    REPO=$(git -C "$CASE" rev-parse --show-toplevel)
    args=(-std=c++20 -O3 -arch=sm_80 -rdc=true --expt-relaxed-constexpr --expt-extended-lambda --use_fast_math -I"$OUT" -I"$REPO/amrex-26.06/Src/Base" -I"$REPO/amrex-26.06/Src/Boundary")
    printf '%q ' nvcc "${args[@]}" "$OUT/production_macro_probe.cu" -o "$OUT/production.ex" > "$OUT/production_command.txt"
    nvcc "${args[@]}" "$OUT/production_macro_probe.cu" -o "$OUT/production.ex" > "$OUT/production_compile.log" 2>&1
    "$OUT/production.ex" > "$OUT/production.json"
fi
