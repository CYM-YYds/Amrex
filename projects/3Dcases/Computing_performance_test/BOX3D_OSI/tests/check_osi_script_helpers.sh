#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../scripts" && pwd)"
source "${SCRIPT_DIR}/osi_experiment_common.sh"
source "${SCRIPT_DIR}/osi_lockstep_args.sh"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/box3d-script-check.XXXXXX")"
trap 'rm -rf "${work_dir}"' EXIT
printf 'node-a 2\nnode-b 1\n' > "${work_dir}/allocation"
CCS_ALLOC_FILE="${work_dir}/allocation"
osi_write_hostfile "${work_dir}/hostfile"
diff -u <(printf 'node-a slots=2\nnode-b slots=1\n') "${work_dir}/hostfile"

# 模拟启动器记录完整 argv，同时保持被启动程序的失败码。
mpirun() {
    printf '%s\n' "$@" > "${work_dir}/argv"
    return "${MOCK_EXIT:-0}"
}
osi_launch_mpi hmpi "${work_dir}/hostfile" 2 2 /tmp/app.ex /tmp/inputs \
    max_step=64 'amr.n_cell=64 64 64'
mapfile -t actual < "${work_dir}/argv"
[[ " ${actual[*]} " == *' -n 2 -npernode 2 '* ]]
[[ " ${actual[*]} " == *' /tmp/app.ex /tmp/inputs max_step=64 amr.n_cell=64 64 64 '* ]]
[[ " ${actual[*]} " != *'--mca pml ucx'* ]]
osi_launch_mpi cuda_aware "${work_dir}/hostfile" 1 1 /tmp/app.ex /tmp/inputs
mapfile -t actual < "${work_dir}/argv"
[[ " ${actual[*]} " == *'--mca pml ucx'* ]]
MOCK_EXIT=37
if osi_launch_mpi hmpi "${work_dir}/hostfile" 2 2 /tmp/app.ex /tmp/inputs; then
    echo 'ERROR: MPI launcher failure was swallowed' >&2
    exit 1
else
    [[ $? -eq 37 ]]
fi

for spec in '64 true 32' '128 false 32' '256 true 1'; do
    read -r steps continue interval <<< "${spec}"
    osi_lockstep_args "${steps}" "${continue}" "${interval}"
    [[ " ${AMREX_RUN_ARGS} " == *" max_step=${steps} "* ]]
    [[ " ${AMREX_RUN_ARGS} " == *" verification.osi_ab_continue_on_mismatch=${continue} "* ]]
    [[ " ${AMREX_RUN_ARGS} " == *" verification.osi_step_entry_check_interval=${interval} "* ]]
    [[ " ${AMREX_RUN_ARGS} " != *'lbm.osi_local_direct='* ]]
done
osi_record_command "${work_dir}/command" /tmp/app.ex /tmp/inputs 'amr.n_cell=64 64 64'
[[ $(cat "${work_dir}/command") == *'amr.n_cell=64\ 64\ 64'* ]]
echo 'PASS OSI script hostfile, launcher, failure propagation and lockstep arguments'

# 在隔离算例中模拟提交节点到作业节点的两次进入，绝不调用真实调度器或求解器。
mkdir -p "${work_dir}/case/scripts" "${work_dir}/bin"
cp "${SCRIPT_DIR}/submit_osi_lockstep.sh" "${SCRIPT_DIR}/submit_re1000_128k.sh" \
    "${SCRIPT_DIR}/osi_lockstep_args.sh" "${work_dir}/case/scripts/"
cat > "${work_dir}/case/scripts/submit.sh" <<'MOCK_SUBMIT'
#!/bin/bash
printf '%s\n' "${AMREX_RUN_ARGS}" > "${MOCK_ENTRY_DIR}/run_args"
pwd > "${MOCK_ENTRY_DIR}/run_cwd"
exit "${MOCK_EXIT:-0}"
MOCK_SUBMIT
cat > "${work_dir}/bin/dsub" <<'MOCK_DSUB'
#!/bin/bash
set -euo pipefail
printf '%s\n' "$@" > "${MOCK_ENTRY_DIR}/dsub_args"
if [[ ${MOCK_DSUB_EXIT:-0} != 0 ]]; then exit "${MOCK_DSUB_EXIT}"; fi
while (( $# )); do
    case "$1" in
        -s) entry=$2 ;;
        -x) selection=${2#all;}; selection=${selection%;} ;;
    esac
    shift 2
done
env CCS_JOB_ID=mock "${selection}" bash "${entry}"
MOCK_DSUB
chmod +x "${work_dir}/case/scripts/submit.sh" "${work_dir}/bin/dsub"
export MOCK_ENTRY_DIR="${work_dir}" PATH="${work_dir}/bin:${PATH}" MOCK_EXIT=0
unset CCS_JOB_ID BOX3D_LOCKSTEP_STEPS BOX3D_RE1000_MODE
export AMREX_RUN_ARGS='sentinel=1 max_step=7'
for spec in '64 true 32' '128 false 32' '256 true 1'; do
    read -r steps continue interval <<< "${spec}"
    bash "${work_dir}/case/scripts/submit_osi_lockstep.sh" "${steps}"
    actual_args=$(cat "${work_dir}/run_args")
    [[ " ${actual_args} " == *" max_step=${steps} "* ]]
    [[ " ${actual_args} " == *" verification.osi_ab_continue_on_mismatch=${continue} "* ]]
    [[ " ${actual_args} " == *" verification.osi_step_entry_check_interval=${interval} "* ]]
    [[ " ${actual_args} " != *'sentinel=1'* ]]
    grep -Fxq "box3d_osi_lockstep_alllevels_${steps}" "${work_dir}/dsub_args"
    [[ $(grep -Fxc "logs/submit/%J-osi-lockstep-alllevels-${steps}.log" "${work_dir}/dsub_args") == 2 ]]
    [[ $(cat "${work_dir}/run_cwd") == "${work_dir}/case" ]]
done
for spec in 'ab 0' 'osi 1'; do
    read -r mode stream_mode <<< "${spec}"
    bash "${work_dir}/case/scripts/submit_re1000_128k.sh" "${mode}"
    actual_args=$(cat "${work_dir}/run_args")
    [[ "${actual_args}" == sentinel=1\ max_step=7* ]]
    [[ " ${actual_args} " == *" lbm.stream_mode=${stream_mode} "* ]]
    [[ " ${actual_args} " == *' max_step=128000 '* ]]
    grep -Fxq "box3d_re1000_128k_${mode}" "${work_dir}/dsub_args"
    [[ $(grep -Fxc "logs/submit/%J-re1000-128k-${mode}.log" "${work_dir}/dsub_args") == 2 ]]
done
for entry in submit_osi_lockstep.sh submit_re1000_128k.sh; do
    rm -f "${work_dir}/dsub_args"
    bash "${work_dir}/case/scripts/${entry}" --help > /dev/null
    [[ ! -e "${work_dir}/dsub_args" ]]
    if bash "${work_dir}/case/scripts/${entry}" invalid > /dev/null 2>&1; then
        echo 'ERROR: 非法预设未被拒绝' >&2; exit 1
    else
        [[ $? -eq 2 && ! -e "${work_dir}/dsub_args" ]]
    fi
    if MOCK_EXIT=37 bash "${work_dir}/case/scripts/${entry}"; then
        echo 'ERROR: 作业内失败码未传递' >&2; exit 1
    else
        [[ $? -eq 37 ]]
    fi
    if MOCK_DSUB_EXIT=19 bash "${work_dir}/case/scripts/${entry}"; then
        echo 'ERROR: 提交失败码未传递' >&2; exit 1
    else
        [[ $? -eq 19 ]]
    fi
done
echo 'PASS unified preset submission, job re-entry, naming, invalid input and failure propagation'
