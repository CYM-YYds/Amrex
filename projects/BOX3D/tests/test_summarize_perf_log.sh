#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

sample_log="$tmp_dir/sample.log"
out_dir="$tmp_dir/out"

cat > "$sample_log" <<'LOG'
step1000 compute_time: 3000 ms regrid_time: 100 ms JaberCycle_time: 2500 ms
step1000 perf(s): interp=1 collide=2 stream=3 average=4 comm=5 boundary=6 swap=0.1 solv=20 total=30 steps=1000 MLUPS_solv=111.5 MLUPS_total=100
step2000 compute_time: 4000 ms regrid_time: 100 ms JaberCycle_time: 3500 ms
step2000 perf(s): interp=10 collide=20 stream=30 average=40 comm=50 boundary=60 swap=0.2 solv=200 total=300 steps=1000 MLUPS_solv=222.5 MLUPS_total=200
step3000 compute_time: 5000 ms regrid_time: 100 ms JaberCycle_time: 4500 ms
step3000 perf(s): interp=100 collide=200 stream=300 average=400 comm=500 boundary=600 swap=0.3 solv=2000 total=3000 steps=1000 MLUPS_solv=333.5 MLUPS_total=300
LOG

"$repo_root/projects/BOX3D/tests/summarize_perf_log.sh" "$sample_log" 2000 "$out_dir"

mlups_file="$out_dir/sample_mlups_solv_upto_2000.tsv"
summary_file="$out_dir/sample_summary_upto_2000.tsv"

test -f "$mlups_file"
test -f "$summary_file"

grep -qx $'step\tMLUPS_solv' "$mlups_file"
grep -qx $'1000\t111.5' "$mlups_file"
grep -qx $'2000\t222.5' "$mlups_file"
if grep -q $'3000\t333.5' "$mlups_file"; then
    echo "unexpected step beyond limit in MLUPS output"
    exit 1
fi

grep -qx $'Interp.\t11.000000000' "$summary_file"
grep -qx $'Average\t44.000000000' "$summary_file"
grep -qx $'Comm.\t55.000000000' "$summary_file"
grep -qx $'Collide\t22.000000000' "$summary_file"
grep -qx $'Stream\t33.000000000' "$summary_file"
grep -qx $'Boundary\t66.000000000' "$summary_file"
grep -qx $'Solv.\t220.000000000' "$summary_file"
grep -qx $'JaberCycle_time\t6.000000000' "$summary_file"

echo "summarize_perf_log test passed"
