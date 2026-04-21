#!/bin/bash
set -euo pipefail

CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

nghost=$(awk '/int nghost =/ { gsub(/[^0-9]/, "", $4); print $4; exit }' "${CASE_DIR}/src/AmrCoreLBM.H")
max_n=$(rg -o 'Collide\(lev, [0-9]+\)|Stream\(lev, [0-9]+\)|SwapLevel\(lev, [0-9]+\)' "${CASE_DIR}/src/main.cpp" |
    awk -F'[,)]' '{ gsub(/ /, "", $2); if ($2 > max) max = $2 } END { print max + 0 }')

if [[ -z "${nghost}" ]]; then
    echo "failed to parse nghost" >&2
    exit 1
fi

if ((nghost < max_n)); then
    echo "nghost=${nghost} is smaller than the widest JaberCycle stencil/ghost use n=${max_n}" >&2
    exit 1
fi

echo "nghost=${nghost} covers JaberCycle n=${max_n}"
