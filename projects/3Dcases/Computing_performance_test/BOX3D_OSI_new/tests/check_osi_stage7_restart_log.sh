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

particle_step16=$(mktemp /tmp/box3d_osi_stage7_particle.XXXXXX)
trap 'rm -f "${particle_step16}"' EXIT
grep '^particle_checksum: step=16 ' "${LOG_FILE}" > "${particle_step16}"
if [[ $(wc -l < "${particle_step16}") -ne 2 ]]; then
    echo "ERROR: expected uninterrupted and restarted particle checksums at step 16" >&2
    exit 5
fi
awk '
function abs(x) { return x < 0 ? -x : x }
{
    row += 1
    for (i = 1; i <= NF; ++i) {
        split($i, field, "=")
        if (field[1] != "particle_checksum:" && field[2] != "") {
            value[row, field[1]] = field[2]
        }
    }
}
END {
    if (value[1,"count"] != value[2,"count"]) exit 1
    for (key in value) {
        split(key, key_part, SUBSEP)
        if (key_part[1] != 1 || key_part[2] == "count" ||
            key_part[2] == "step" || key_part[2] == "particle") continue
        a = value[1,key_part[2]] + 0.0
        b = value[2,key_part[2]] + 0.0
        scale = abs(a) > abs(b) ? abs(a) : abs(b)
        if (abs(a-b) > 1.0e-9 + 1.0e-13*scale) exit 2
    }
}
' "${particle_step16}" || {
    echo "ERROR: particle checksum changed beyond tolerance across checkpoint/restart" >&2
    cat "${particle_step16}" >&2
    exit 6
}

particle_count=$(awk '{for (i=1;i<=NF;++i) {split($i,f,"="); if (f[1]=="count") print f[2]}}' "${particle_step16}" | head -1)
if [[ -z "${particle_count}" || ${particle_count} -le 0 ]]; then
    echo "ERROR: invalid restarted particle count '${particle_count}'" >&2
    exit 7
fi

echo "OSI stage-7 restart passed: canonical DDF Linf=${global_linf}, particle count=${particle_count}, particle checksum preserved"
