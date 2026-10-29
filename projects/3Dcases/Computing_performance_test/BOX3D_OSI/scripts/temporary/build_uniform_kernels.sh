#!/usr/bin/env bash
# 本次单层同步/无mask实验的临时构建入口；随结果保留供复现。
set -euo pipefail
CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EXP="$CASE_DIR/runs/temporary/uniform_kernels/round1"
for variant in baseline sync nomask; do
    [[ ! -e "$EXP/$variant.ex" ]]
    cp -a "$EXP/$variant/src" "$EXP/$variant/config" "$EXP/build/"
    # 共用本次复制的编译缓存，强制重新编译两处实验源，防止恢复旧时间戳时混入上一候选。
    touch "$EXP/build/src/AmrCoreLBM_advance.cpp" "$EXP/build/src/AmrCoreLBM_osi.cpp"
    (
        cd "$EXP/build"
        GEN_CCDB=0 MAKE_J=8 ./scripts/compile.sh --no-submit
    ) > "$EXP/${variant}_compile.log" 2>&1
    cp "$EXP/build/main3d.gnu.FLOAT.TPROF.MPI.CUDA.ex" "$EXP/$variant.ex"
    sha256sum "$EXP/$variant.ex" > "$EXP/$variant.sha256"
    echo "BUILT $variant"
done
