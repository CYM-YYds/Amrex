#!/usr/bin/env bash
set -euo pipefail

case_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

input="$tmp_dir/Cf_steady2_sample.dat"
output="$tmp_dir/Cf_steady2_sample_retheta.dat"

cat > "$input" <<'DATA'
# q_inf               = 0.00135
# theta(rad)	Cd_x_density	Cf(from_tangent_force)	Cp(from_normal_force)
-3.0	1.0	10.0	100.0
-1.0	2.0	20.0	200.0
0.0	3.0	30.0	300.0
1.0	4.0	40.0	400.0
3.0	5.0	50.0	500.0
DATA

python3 "$case_dir/scripts/sort_by_redefined_theta.py" "$input" --output "$output"

if [[ ! -s "$output" ]]; then
    echo "Expected non-empty output file: $output" >&2
    exit 1
fi

grep -Fq "# redefined_theta_formula = (mod(pi - theta(rad) + pi, 2*pi) - pi)" "$output"
grep -q "# theta_new(rad).*theta(rad).*Cd_x_density" "$output"

awk '
    $1 !~ /^#/ {
        if (NF != 5) {
            printf("Expected 5 data columns, got %d\n", NF) > "/dev/stderr"
            exit 1
        }
        if (count > 0 && $1 < prev - 1.0e-12) {
            printf("theta_new is not sorted: %.17g after %.17g\n", $1, prev) > "/dev/stderr"
            exit 1
        }
        original[count] = $2
        newtheta[count] = $1
        prev = $1
        count++
    }
    END {
        if (count != 5) {
            printf("Expected 5 rows, got %d\n", count) > "/dev/stderr"
            exit 1
        }
        expected_original[0] = 0.0
        expected_original[1] = -1.0
        expected_original[2] = -3.0
        expected_original[3] = 3.0
        expected_original[4] = 1.0
        for (i = 0; i < count; ++i) {
            if (sqrt((original[i] - expected_original[i])^2) > 1.0e-12) {
                printf("Unexpected original theta at row %d: %.17g\n", i + 1, original[i]) > "/dev/stderr"
                exit 1
            }
        }
    }
' "$output"
