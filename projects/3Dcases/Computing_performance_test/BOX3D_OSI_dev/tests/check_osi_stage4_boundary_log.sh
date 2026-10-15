#!/bin/bash

set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "Usage: $0 <single-rank-log> [two-rank-log]" >&2
    exit 2
fi

for log_file in "$@"; do
    if [[ ! -r "${log_file}" ]]; then
        echo "ERROR: cannot read ${log_file}" >&2
        exit 2
    fi
    grep -Eq '\[OSI boundary\] ranks=[12] boxes=64 seed_pattern=1' "${log_file}"
    grep -Eq '\[OSI boundary\].*boundary_scratch_values=[1-9][0-9]*' "${log_file}"
    grep -q 'AMReX .* finalized' "${log_file}"
done
grep -q '\[OSI boundary\] ranks=1 boxes=64 seed_pattern=1' "$1"
if [[ $# -eq 2 ]]; then
    grep -q '\[OSI boundary\] ranks=2 boxes=64 seed_pattern=1' "$2"
fi

extract_values() {
    local log_file=$1
    local output_file=$2

    awk '
    /^osi_ab:/ {
        step = phase = linf = ""
        for (i = 1; i <= NF; ++i) {
            split($i, field, "=")
            if (field[1] == "step") step = field[2]
            if (field[1] == "phase") phase = field[2]
            if (field[1] == "linf") linf = field[2]
        }
        if (step == "" || phase == "" || linf == "" || linf + 0 > 1.0e-12) {
            exit 3
        }
        if (phase + 0 != step + 0) exit 4
        if (linf + 0 > max_linf) max_linf = linf + 0
        print step, phase, linf
        count += 1
    }
    END {
        if (count != 32) exit 5
        printf "%.17g\n", max_linf > "/dev/stderr"
    }
    ' "${log_file}" > "${output_file}"
}

single_values=$(mktemp /tmp/box3d_osi_stage4_single.XXXXXX)
mpi_values=$(mktemp /tmp/box3d_osi_stage4_mpi.XXXXXX)
trap 'rm -f "${single_values}" "${mpi_values}"' EXIT
extract_values "$1" "${single_values}"

if [[ $# -eq 2 ]]; then
    extract_values "$2" "${mpi_values}"
    diff -u "${single_values}" "${mpi_values}"
fi

echo "OSI stage-4 boundary logs passed: 32 steps, valid-DDF tolerance and rank sequence checks passed"
