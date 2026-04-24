#!/usr/bin/env bash
set -euo pipefail

case_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

input="$tmp_dir/Cf_steady2_folded_sample.dat"
output="$tmp_dir/Cf_steady2_folded_sample_smooth.dat"

cat > "$input" <<'DATA'
# q_inf               = 0.00135
# D_phys              = 0.025
# ds(actual arc)      = 0.000156454
# Cd_v(integral Cf)   = 0.5
# theta(rad)	Cd_x_density	Cf(from_tangent_force)	Cp(from_normal_force)
-2.356194490192345	0.0	0.9571067812	0.0
-1.570796326794897	0.0	1.0	0.0
-0.785398163397448	0.0	0.4571067812	0.0
0.0	0.0	0.0	0.0
0.785398163397448	0.0	0.4571067812	0.0
1.570796326794897	0.0	1.0	0.0
2.356194490192345	0.0	0.9571067812	0.0
3.141592653589793	0.0	1.224646799e-16	0.0
DATA

python3 "$case_dir/scripts/smooth_cf_profile_folded_periodic.py" "$input" \
    --keep-modes 1 \
    --output "$output"

if [[ ! -s "$output" ]]; then
    echo "Expected non-empty output file: $output" >&2
    exit 1
fi

grep -q "# smoothing_method      = folded_periodic_fourier_lowpass" "$output"
grep -q "# unfold_rule           = negate theta<0 before periodic Fourier" "$output"
grep -q "# refold_rule           = negate theta<0 after periodic Fourier" "$output"
grep -q "# theta(rad).*Cf_smooth" "$output"

awk '
    $1 !~ /^#/ {
        if (NF != 5) {
            printf("Expected 5 data columns, got %d\n", NF) > "/dev/stderr"
            exit 1
        }
        if ($1 < 0.0) neg[$1] = $5
        if ($1 > 0.0) pos[$1] = $5
    }
    END {
        if (sqrt((neg[-0.785398163397448] - pos[0.785398163397448])^2) > 1e-9) {
            print "Expected folded periodic smoothing to preserve left/right folded symmetry at |theta|=pi/4" > "/dev/stderr"
            exit 1
        }
        if (sqrt((neg[-1.570796326794897] - pos[1.570796326794897])^2) > 1e-9) {
            print "Expected folded periodic smoothing to preserve left/right folded symmetry at |theta|=pi/2" > "/dev/stderr"
            exit 1
        }
    }
' "$output"
