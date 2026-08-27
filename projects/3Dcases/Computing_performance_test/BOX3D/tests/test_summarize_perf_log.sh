#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT

log_file="$tmp_dir/perf.log"
out_dir="$tmp_dir/out"

printf '%s\n' \
    'step1000 compute_time: 1 JaberCycle_time: 2000' \
    'step1000 perf(s): interp=1 average=2 comm=3 collide=4 stream=5 boundary=6 solv=21 MLUPS_solv=100' \
    'step2000 compute_time: 2 JaberCycle_time: 3000' \
    'step2000 perf(s): interp=10 average=20 comm=30 collide=40 stream=50 boundary=60 solv=210 MLUPS_solv=200' \
    'step3000 compute_time: 3 JaberCycle_time: 4000' \
    'step3000 perf(s): interp=100 average=200 comm=300 collide=400 stream=500 boundary=600 solv=2100 MLUPS_solv=300' \
    > "$log_file"

"$script_dir/summarize_perf_log.sh" "$log_file" 2000 "$out_dir"

summary="$out_dir/perf_summary_upto_2000.tsv"
mlups="$out_dir/perf_mlups_solv_upto_2000.tsv"

grep -Fqx $'Interp.\t11.000000000' "$summary"
grep -Fqx $'Average\t22.000000000' "$summary"
grep -Fqx $'Solv.\t231.000000000' "$summary"
grep -Fqx $'JaberCycle_time\t5.000000000' "$summary"
grep -Fqx $'1000\t100' "$mlups"
grep -Fqx $'2000\t200' "$mlups"
test "$(wc -l < "$mlups")" -eq 3

printf 'PASS: summarize_perf_log.sh honors max_step and aggregates timing windows.\n'
