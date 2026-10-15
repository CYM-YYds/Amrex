#!/bin/bash

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <single-rank-log> <two-rank-log>" >&2
    exit 2
fi

for log_file in "$@"; do
    if [[ ! -r "${log_file}" ]]; then
        echo "ERROR: cannot read ${log_file}" >&2
        exit 2
    fi

    awk '
        function field_value(name,    i, a) {
            for (i = 1; i <= NF; ++i) {
                split($i, a, "=")
                if (a[1] == name) return a[2]
            }
            return ""
        }

        /^OSI_SYNC_BATCH_AB_BEGIN/ {
            mode = "ab"
            ranks = field_value("ranks") + 0
            batch = field_value("batch") + 0
            ab_count = 0
            max_linf = 0
            next
        }

        /^OSI_SYNC_BATCH_PERF_BEGIN/ {
            mode = "perf"
            ranks = field_value("ranks") + 0
            batch = field_value("batch") + 0
            perf_found = 0
            max_comm = 0
            max_solv = 0
            wall_mlups = 0
            next
        }

        /^\[OSI periodic\]/ {
            actual_batch = field_value("sync_batch_components") + 0
            actual_batches = field_value("sync_batches") + 0
            sync_values = field_value("sync_values") + 0
            expected_batches = int((27 + batch - 1) / batch)
            expected_values = 512000 * batch
            if (actual_batch != batch || actual_batches != expected_batches ||
                sync_values != expected_values) {
                print "ERROR: startup diagnostics mismatch for ranks=" ranks \
                      " batch=" batch > "/dev/stderr"
                exit 3
            }
        }

        mode == "ab" && /^osi_ab:/ {
            linf = field_value("linf") + 0
            if (linf > 1.0e-12) {
                print "ERROR: linf exceeds tolerance for ranks=" ranks \
                      " batch=" batch ": " linf > "/dev/stderr"
                exit 4
            }
            if (linf > max_linf) max_linf = linf
            ++ab_count
        }

        /^OSI_SYNC_BATCH_AB_END/ {
            if (ab_count != 32) {
                print "ERROR: expected 32 A-B steps for ranks=" ranks \
                      " batch=" batch ", got " ab_count > "/dev/stderr"
                exit 5
            }
            printf "correctness ranks=%d batch=%d steps=%d max_linf=%.17g\n", \
                   ranks, batch, ab_count, max_linf
            mode = ""
        }

        mode == "perf" && /^step2000 perf\(s\):/ {
            comm = field_value("comm") + 0
            solv = field_value("solv") + 0
            mlups = field_value("MLUPS_solv") + 0
            if (comm > max_comm) max_comm = comm
            if (solv > max_solv) {
                max_solv = solv
                wall_mlups = mlups
            }
            perf_found = 1
        }

        /^OSI_SYNC_BATCH_PERF_END/ {
            if (!perf_found) {
                print "ERROR: missing step2000 performance window for ranks=" ranks \
                      " batch=" batch > "/dev/stderr"
                exit 6
            }
            printf "performance ranks=%d batch=%d comm_s=%.9g solv_s=%.9g MLUPS_solv=%.9g\n", \
                   ranks, batch, max_comm, max_solv, wall_mlups
            mode = ""
        }
    ' "${log_file}"

    finalized=$(grep -c 'AMReX .* finalized' "${log_file}")
    if [[ ${finalized} -ne 6 ]]; then
        echo "ERROR: expected 6 finalized runs in ${log_file}, got ${finalized}" >&2
        exit 7
    fi
done
