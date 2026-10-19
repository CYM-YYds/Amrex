#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_re1000_128k_osi
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-re1000-128k-osi.log
#DSUB -e logs/submit/%J-re1000-128k-osi.log
#DSUB -l cuda122
set -euo pipefail

if [[ ${1:-} == --help ]]; then
    echo '用法：./scripts/submit_re1000_128k.sh [ab|osi]（默认 osi；在提交节点执行）'
    exit 0
fi
if (( $# > 1 )); then
    echo 'ERROR: 只接受一个推进模式：ab 或 osi' >&2
    exit 2
fi
mode=${1:-${BOX3D_RE1000_MODE:-osi}}
case "${mode}" in
    ab) stream_mode=0 ;;
    osi) stream_mode=1 ;;
    *) echo 'ERROR: 推进模式必须为 ab 或 osi' >&2; exit 2 ;;
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${CASE_DIR}"
if [[ -z ${CCS_JOB_ID:-} ]]; then
    # 保留两种历史模式的作业名与日志名，公共资源声明继续由调度器读取。
    export CCS_CLI_HOME=${CCS_CLI_HOME:-/opt/batch/cli}
    mkdir -p logs/submit
    exec dsub -n "box3d_re1000_128k_${mode}" \
        -o "logs/submit/%J-re1000-128k-${mode}.log" \
        -e "logs/submit/%J-re1000-128k-${mode}.log" \
        -x "all;BOX3D_RE1000_MODE=${mode};" -s "${SCRIPT_DIR}/submit_re1000_128k.sh"
fi

# 保持原覆盖顺序：用户附加参数在前，本实验固定参数在后。
export AMREX_RUN_ARGS="${AMREX_RUN_ARGS:-} \
max_step=128000 \
lbm.reynolds_number=1000.0 \
lbm.stream_mode=${stream_mode} \
amr.max_level=2 \
amr.regrid_int=32 \
amr.plot_int=-1 \
performance.report_int=3200 \
checkpoint.begin_step=0 \
checkpoint.chk_int=32000 \
checkpoint.chk_prefix=chk \
checkpoint.keep_latest_only=true \
checkpoint.write_particles=false \
verification.convergence_enabled=false \
verification.osi_seed_pattern=false \
verification.osi_ab_check=false"

exec "${SCRIPT_DIR}/submit.sh"
