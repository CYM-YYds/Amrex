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
