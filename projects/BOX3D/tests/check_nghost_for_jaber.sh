#!/bin/bash
set -euo pipefail

CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

nghost=$(awk '/int nghost =/ { gsub(/[^0-9]/, "", $4); print $4; exit }' "${CASE_DIR}/src/AmrCoreLBM.H")

if [[ -z "${nghost}" ]]; then
    echo "failed to parse nghost" >&2
    exit 1
fi

if ! rg -q 'int ghostCells\(\) const' "${CASE_DIR}/src/AmrCoreLBM.H"; then
    echo "AmrCoreLBM does not expose ghostCells() for main.cpp alignment" >&2
    exit 1
fi

if rg -q 'Collide\(lev, 4\)|Stream\(lev, 4\)|SwapLevel\(lev, 4\)' "${CASE_DIR}/src/main.cpp"; then
    echo "main.cpp still hardcodes JaberCycle ghost-cell width 4" >&2
    exit 1
fi

if ! rg -q 'const int nghost = lid\.ghostCells\(\);' "${CASE_DIR}/src/main.cpp"; then
    echo "main.cpp does not read ghost width from AmrCoreLBM::ghostCells()" >&2
    exit 1
fi

echo "main.cpp uses AmrCoreLBM ghostCells(); AmrCoreLBM.H nghost=${nghost}"
