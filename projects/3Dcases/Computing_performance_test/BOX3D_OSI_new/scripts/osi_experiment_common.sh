#!/usr/bin/env bash

# 仅整理当前算例实验入口的 hostfile 与 MPI 启动；模块和可执行文件由调用方选择。
osi_write_hostfile() {
    local destination=$1
    awk '{ if (length($1) > 0 && length($2) > 0) print $1 " slots=" $2 }' \
        "${CCS_ALLOC_FILE:?CCS_ALLOC_FILE is unavailable}" > "${destination}"
}

osi_launch_mpi() {
    local flavor=$1 hostfile=$2 ranks=$3 per_node=$4 executable=$5 inputs=$6
    shift 6
    local mpi_options=(-hostfile "${hostfile}" -n "${ranks}" -npernode "${per_node}"
        -x PATH -x LD_LIBRARY_PATH)
    case "${flavor}" in
        hmpi) ;;
        cuda_aware) mpi_options+=(-x OPAL_PREFIX --mca pml ucx) ;;
        *) echo "ERROR: unknown MPI flavor: ${flavor}" >&2; return 2 ;;
    esac
    mpirun "${mpi_options[@]}" \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
        bash -lc 'export CUDA_VISIBLE_DEVICES=${OMPI_COMM_WORLD_LOCAL_RANK:-${MPI_LOCALRANKID:-0}}; exec "$@"' bash \
        "${executable}" "${inputs}" "$@"
}

# 冻结每组实际命令，便于跨版本比较时确认输入和覆盖参数完全一致。
osi_record_command() {
    local destination=$1
    shift
    printf '%q ' "$@" > "${destination}"
    printf '\n' >> "${destination}"
}
