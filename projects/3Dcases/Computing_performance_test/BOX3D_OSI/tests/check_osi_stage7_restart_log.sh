#!/bin/bash

set -euo pipefail

if [[ $# -ne 1 || ! -r "$1" ]]; then
    echo "Usage: $0 <stage7-restart-log>" >&2
    exit 2
fi

LOG_FILE=$1
grep -q 'stage7_case: uninterrupted ranks=1 steps=16' "${LOG_FILE}"
grep -q 'stage7_case: checkpoint_source ranks=1 steps=8' "${LOG_FILE}"
grep -q 'stage7_case: restarted ranks=2 begin_step=8 final_step=16' "${LOG_FILE}"
grep -q 'stage7_case: ab_uninterrupted ranks=1 steps=8' "${LOG_FILE}"
grep -q 'stage7_case: ab_checkpoint_source ranks=1 steps=4' "${LOG_FILE}"
grep -q 'stage7_case: ab_restarted ranks=2 begin_step=4 final_step=8' "${LOG_FILE}"
grep -q '\[Checkpoint\] Restarted at step=8' "${LOG_FILE}"
grep -q '\[Checkpoint\] Restarted at step=4' "${LOG_FILE}"
grep -q 'stage7_artifacts: .*checkpoint_layout=canonical_osi_single_array_v1.*ab_layout=canonical_ab_two_array_v1.*plot=plt000016' "${LOG_FILE}"
grep -Eq 'ddf_cell_norm_level: lev=0 .*active_linf=0' "${LOG_FILE}"
grep -Eq 'ddf_cell_norm_level: lev=1 .*active_linf=0' "${LOG_FILE}"

finalized_count=$(grep -c 'AMReX .* finalized' "${LOG_FILE}")
if [[ ${finalized_count} -ne 6 ]]; then
    echo "ERROR: expected six successful AMReX finalizations, got ${finalized_count}" >&2
    exit 3
fi

global_linf=$(awk '
/^ddf_norm_global:/ {
    for (i = 1; i <= NF; ++i) {
        split($i, field, "=")
        if (field[1] == "linf") {
            value = field[2]
            if (value + 0.0 > maximum + 0.0) maximum = value
            count += 1
        }
    }
}
END {
    if (count != 2) exit 4
    print maximum + 0.0
}
' "${LOG_FILE}")

awk -v value="${global_linf}" 'BEGIN { if (value + 0.0 > 1.0e-12) exit 1 }'
echo "OSI stage-7 restart passed: 1-rank checkpoint, 2-rank restart, canonical layout, plot output, global Linf=${global_linf}"
