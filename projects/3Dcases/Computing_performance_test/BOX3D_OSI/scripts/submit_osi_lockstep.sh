#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_osi_lockstep_alllevels_64
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-osi-lockstep-alllevels-64.log
#DSUB -e logs/submit/%J-osi-lockstep-alllevels-64.log
#DSUB -l cuda122
set -euo pipefail

if [[ ${1:-} == --help ]]; then
    echo '用法：./scripts/submit_osi_lockstep.sh [64|128|256]（默认 64；在提交节点执行）'
    exit 0
fi
if (( $# > 1 )); then
    echo 'ERROR: 只接受一个锁步预设：64、128 或 256' >&2
    exit 2
fi
steps=${1:-${BOX3D_LOCKSTEP_STEPS:-64}}
case "${steps}" in
    64) continue_on_mismatch=true; check_interval=32 ;;
    128) continue_on_mismatch=false; check_interval=32 ;;
    256) continue_on_mismatch=true; check_interval=1 ;;
    *) echo 'ERROR: 锁步预设必须为 64、128 或 256' >&2; exit 2 ;;
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${CASE_DIR}"
if [[ -z ${CCS_JOB_ID:-} ]]; then
    # 提交时指定动态标识，并显式传递预设；作业内再次进入本入口后不重复提交。
    export CCS_CLI_HOME=${CCS_CLI_HOME:-/opt/batch/cli}
    mkdir -p logs/submit
    exec dsub -n "box3d_osi_lockstep_alllevels_${steps}" \
        -o "logs/submit/%J-osi-lockstep-alllevels-${steps}.log" \
        -e "logs/submit/%J-osi-lockstep-alllevels-${steps}.log" \
        -x "all;BOX3D_LOCKSTEP_STEPS=${steps};" -s "${SCRIPT_DIR}/submit_osi_lockstep.sh"
fi

source "${SCRIPT_DIR}/osi_lockstep_args.sh"
osi_lockstep_args "${steps}" "${continue_on_mismatch}" "${check_interval}"
exec "${SCRIPT_DIR}/submit.sh"
