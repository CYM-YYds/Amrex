#!/bin/bash
# 用途：本次 IBM 参数开关及重启回归；保留为结果复现入口。
#DSUB --job_type cosched
#DSUB -n cylinder_ibm_switch
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=8;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o runs/temporary/ibm_runtime_switch/%J.log
#DSUB -e runs/temporary/ibm_runtime_switch/%J.log
#DSUB -l cuda122
set -euo pipefail
ulimit -c 0
CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
TASK_DIR="${CASE_DIR}/runs/temporary/ibm_runtime_switch"
export AMREX_EXECUTABLE="${TASK_DIR}/main3d.gnu.MPI.CUDA.ex"
run_case() {
    local name="$1" args="$2"
    mkdir -p "${TASK_DIR}/${name}/data"
    cd "${TASK_DIR}/${name}"
    cp "${TASK_DIR}/inputs" inputs
    export AMREX_INPUT_FILE="${PWD}/inputs"
    export AMREX_RUN_ARGS="max_step=64 amr.regrid_int=10 amr.plot_int=64 checkpoint.chk_int=32 checkpoint.keep_latest_only=false ${args}"
    printf '%s\n' "${AMREX_EXECUTABLE} ${AMREX_INPUT_FILE} ${AMREX_RUN_ARGS}" > command.txt
    bash "${CASE_DIR}/../../../scripts/submit.sh" > run.log 2>&1
}
case "${IBM_VALIDATION_STAGE:-matrix}" in
baseline)
    export AMREX_EXECUTABLE="${TASK_DIR}/baseline_build/main3d.gnu.MPI.CUDA.ex"
    run_case baseline ''
    exit 0
    ;;
negative)
    for setting in 'ibm.geometry=invalid' 'ibm.method=invalid'; do
        name="invalid_${setting%%=*}"
        if run_case "$name" "$setting"; then
            echo "Expected parameter rejection: $setting"; exit 1
        fi
        grep -Eq 'ibm.(geometry|method) must be' "${TASK_DIR}/${name}/run.log"
    done
    mkdir -p "${TASK_DIR}/mismatch"
    cp -a "${TASK_DIR}/off/chk00000032" "${TASK_DIR}/mismatch/"
    if run_case mismatch 'checkpoint.begin_step=32 ibm.enabled=true'; then
        echo 'Expected checkpoint rejection'; exit 1
    fi
    grep -q 'Checkpoint IBM configuration mismatch' "${TASK_DIR}/mismatch/run.log"
    mkdir -p "${TASK_DIR}/legacy_restart"
    cp -a "${TASK_DIR}/baseline/chk00000032" "${TASK_DIR}/legacy_restart/"
    run_case legacy_restart 'checkpoint.begin_step=32'
    echo 'Negative and legacy tests completed'
    exit 0
    ;;
matrix) ;;
*) echo 'Invalid IBM_VALIDATION_STAGE'; exit 2;;
esac
run_case implicit ''
run_case sphere_idf 'ibm.enabled=true ibm.geometry=sphere ibm.method=idf'
run_case sphere_mdf 'ibm.enabled=true ibm.geometry=sphere ibm.method=mdf'
run_case cylinder_mdf 'ibm.enabled=true ibm.geometry=cylinder ibm.method=mdf'
run_case off 'ibm.enabled=false'
for mode in sphere_idf cylinder_mdf off; do
    mkdir -p "${TASK_DIR}/${mode}_restart"
    cp -a "${TASK_DIR}/${mode}/chk00000032" "${TASK_DIR}/${mode}_restart/"
    case "$mode" in
      sphere_idf) args='ibm.enabled=true ibm.geometry=sphere ibm.method=idf';;
      cylinder_mdf) args='ibm.enabled=true ibm.geometry=cylinder ibm.method=mdf';;
      off) args='ibm.enabled=false';;
    esac
    run_case "${mode}_restart" "${args} checkpoint.begin_step=32"
done
printf 'All runs completed\n'
