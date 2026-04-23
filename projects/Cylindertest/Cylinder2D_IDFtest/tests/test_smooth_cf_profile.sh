#!/usr/bin/env bash
set -euo pipefail

case_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

input="$tmp_dir/Cf_steady2_sample.dat"
output="$tmp_dir/Cf_steady2_sample_smooth.dat"

cat > "$input" <<'DATA'
# q_inf               = 0.00135
# D_phys              = 0.025
# ds(actual arc)      = 0.000156454
# Cd_v(integral Cf)   = 0.5
# theta(rad)	Cd_x_density	Cf(from_tangent_force)	Cp(from_normal_force)
-3.141592653589793	0.0	1.0	0.0
-2.356194490192345	0.0	-0.2928932188	0.0
-1.570796326794897	0.0	1.0	0.0
-0.785398163397448	0.0	1.7071067812	0.0
0.0	0.0	1.0	0.0
0.785398163397448	0.0	-0.2928932188	0.0
1.570796326794897	0.0	1.0	0.0
2.356194490192345	0.0	1.7071067812	0.0
DATA

python3 "$case_dir/scripts/smooth_cf_profile.py" "$input" \
    --keep-modes 1 \
    --output "$output"

if [[ ! -s "$output" ]]; then
    echo "Expected non-empty output file: $output" >&2
    exit 1
fi

grep -q "# smoothing_method      = fourier_lowpass" "$output"
grep -q "# keep_modes            = 1" "$output"
grep -q "# theta(rad).*Cf_smooth" "$output"

data_rows="$(awk 'NF && $1 !~ /^#/ { count++ } END { print count + 0 }' "$output")"
if [[ "$data_rows" != "8" ]]; then
    echo "Expected 8 data rows, got $data_rows" >&2
    exit 1
fi

awk '
    $1 !~ /^#/ {
        diff += ($3 - $5) * ($3 - $5)
        if (NF != 5) {
            printf("Expected 5 data columns, got %d\n", NF) > "/dev/stderr"
            exit 1
        }
    }
    END {
        if (diff <= 0.0) {
            print "Expected smoothed Cf to differ from noisy Cf" > "/dev/stderr"
            exit 1
        }
    }
' "$output"
