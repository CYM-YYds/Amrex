#!/bin/bash

set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
    echo "Usage: $0 <single-rank-ab-log> <two-rank-ab-log> [single-array-log]" >&2
    exit 2
fi

single_log=$1
mpi_log=$2
for log_file in "${single_log}" "${mpi_log}"; do
    if [[ ! -r "${log_file}" ]]; then
        echo "ERROR: cannot read ${log_file}" >&2
        exit 2
    fi
done

grep -q '\[OSI periodic\] ranks=1 boxes=64 seed_pattern=1' "${single_log}"
grep -q '\[OSI periodic\] ranks=2 boxes=64 seed_pattern=1' "${mpi_log}"
grep -q 'AMReX .* finalized' "${single_log}"
grep -q 'AMReX .* finalized' "${mpi_log}"

single_values=$(mktemp /tmp/box3d_osi_stage3_single.XXXXXX)
mpi_values=$(mktemp /tmp/box3d_osi_stage3_mpi.XXXXXX)
trap 'rm -f "${single_values}" "${mpi_values}"' EXIT

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
            print step, phase, linf
            count += 1
        }
        END {
            if (count != 32) exit 4
        }
    ' "${log_file}" > "${output_file}"
}

extract_values "${single_log}" "${single_values}"
extract_values "${mpi_log}" "${mpi_values}"
diff -u "${single_values}" "${mpi_values}"

max_linf=$(awk 'BEGIN { max = 0 } { if ($3 > max) max = $3 } END { print max }' \
    "${single_values}")
echo "OSI stage-3 logs passed: 32 steps, identical 1/2-rank sequence, max_linf=${max_linf}"

if [[ $# -eq 3 ]]; then
    single_array_log=$3
    if [[ ! -r "${single_array_log}" ]]; then
        echo "ERROR: cannot read ${single_array_log}" >&2
        exit 2
    fi
    grep -q '\[OSI periodic\].*ab_check=0 full_ddf_arrays=1 .*ring_ngrow=2 grown-fab overlap synchronization enabled' \
        "${single_array_log}"
    grep -q 'AMReX .* finalized' "${single_array_log}"
    if grep -q '^osi_ab:' "${single_array_log}"; then
        echo "ERROR: single-array run unexpectedly advanced the A-B oracle" >&2
        exit 5
    fi
    echo "OSI grown-Fab single-array log passed: full_ddf_arrays=1, ring_ngrow=2, no A-B oracle output"
fi
