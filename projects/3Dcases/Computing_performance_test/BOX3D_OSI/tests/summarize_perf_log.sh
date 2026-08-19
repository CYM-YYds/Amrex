#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 1 || $# -gt 3 ]]; then
    echo "Usage: $0 LOG_FILE [MAX_STEP=64000] [OUT_DIR=LOG_DIR]" >&2
    exit 2
fi

log_file="$1"
max_step="${2:-64000}"
out_dir="${3:-$(dirname "$log_file")}"

if [[ ! -f "$log_file" ]]; then
    echo "log file not found: $log_file" >&2
    exit 1
fi

if ! [[ "$max_step" =~ ^[0-9]+$ ]]; then
    echo "MAX_STEP must be a non-negative integer: $max_step" >&2
    exit 2
fi

mkdir -p "$out_dir"

base_name="$(basename "$log_file")"
stem="${base_name%.*}"
mlups_file="$out_dir/${stem}_mlups_solv_upto_${max_step}.tsv"
summary_file="$out_dir/${stem}_summary_upto_${max_step}.tsv"

awk -v max_step="$max_step" \
    -v mlups_file="$mlups_file" \
    -v summary_file="$summary_file" '
BEGIN {
    print "step\tMLUPS_solv" > mlups_file
}

/step[0-9]+ compute_time:/ {
    line = $0
    step = -1
    if (match(line, /step[0-9]+/)) {
        step = substr(line, RSTART + 4, RLENGTH - 4) + 0
    }
    if (step >= 0 && step <= max_step) {
        for (i = 1; i <= NF; ++i) {
            if ($i == "JaberCycle_time:" && (i + 1) <= NF) {
                jaber_ms += $(i + 1)
                ++jaber_windows
            }
        }
    }
}

/step[0-9]+ perf\(s\):/ {
    line = $0
    step = -1
    if (match(line, /step[0-9]+/)) {
        step = substr(line, RSTART + 4, RLENGTH - 4) + 0
    }
    if (step >= 0 && step <= max_step) {
        ++perf_windows
        for (i = 1; i <= NF; ++i) {
            split($i, kv, "=")
            if (kv[1] == "interp") {
                interp += kv[2]
            } else if (kv[1] == "average") {
                average += kv[2]
            } else if (kv[1] == "comm") {
                comm += kv[2]
            } else if (kv[1] == "collide") {
                collide += kv[2]
            } else if (kv[1] == "stream") {
                stream += kv[2]
            } else if (kv[1] == "boundary") {
                boundary += kv[2]
            } else if (kv[1] == "solv") {
                solv += kv[2]
            } else if (kv[1] == "MLUPS_solv") {
                print step "\t" kv[2] > mlups_file
            }
        }
    }
}

END {
    print "metric\ttotal_seconds" > summary_file
    printf("windows\t%d\n", perf_windows) >> summary_file
    printf("JaberCycle_windows\t%d\n", jaber_windows) >> summary_file
    printf("Interp.\t%.9f\n", interp) >> summary_file
    printf("Average\t%.9f\n", average) >> summary_file
    printf("Comm.\t%.9f\n", comm) >> summary_file
    printf("Collide\t%.9f\n", collide) >> summary_file
    printf("Stream\t%.9f\n", stream) >> summary_file
    printf("Boundary\t%.9f\n", boundary) >> summary_file
    printf("Solv.\t%.9f\n", solv) >> summary_file
    printf("JaberCycle_time\t%.9f\n", jaber_ms / 1000.0) >> summary_file
}
' "$log_file"

echo "MLUPS_solv written to: $mlups_file"
echo "Summary written to: $summary_file"
