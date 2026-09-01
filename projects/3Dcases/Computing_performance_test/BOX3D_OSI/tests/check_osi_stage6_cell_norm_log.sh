#!/bin/bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <stage6-cell-norm-log>" >&2
  exit 2
fi
log=$1
if [[ ! -r "$log" ]]; then
  echo "cannot read log: $log" >&2
  exit 2
fi

awk '
function value(name,    i, field) {
    for (i = 1; i <= NF; ++i) {
        split($i, field, "=")
        if (field[1] == name) return field[2] + 0
    }
    return -1
}
BEGIN { tolerance = 1.0e-12; count = 0; failed = 0; max_linf = 0; max_l1_mean = 0; max_rel_l2 = 0 }
/^ddf_cell_norm_component:/ {
    linf = value("active_linf")
    l1_mean = value("active_l1_mean")
    rel_l2 = value("active_rel_l2")
    if (linf < 0 || l1_mean < 0 || rel_l2 < 0) {
        print "missing active norm field: " $0 > "/dev/stderr"
        failed = 1
    }
    if (linf > tolerance || l1_mean > tolerance || rel_l2 > tolerance) {
        print "active per-cell DDF norm exceeds tolerance: " $0 > "/dev/stderr"
        failed = 1
    }
    if (linf > max_linf) max_linf = linf
    if (l1_mean > max_l1_mean) max_l1_mean = l1_mean
    if (rel_l2 > max_rel_l2) max_rel_l2 = rel_l2
    ++count
}
END {
    if (failed || count != 54) {
        printf "expected 54 level/component records, found %d\n", count > "/dev/stderr"
        exit 1
    }
    printf "OSI stage-6 cell norm passed: records=%d max_active_linf=%.17g max_active_l1_mean=%.17g max_active_rel_l2=%.17g\n", count, max_linf, max_l1_mean, max_rel_l2
}
' "$log"
