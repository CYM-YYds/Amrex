#!/usr/bin/env bash
# 为实际应用返回留证据，区分应用未退出与 MPI 启动器收尾未退出。
set -uo pipefail
rank=${OMPI_COMM_WORLD_RANK:-${PMI_RANK:-0}}
size=${OMPI_COMM_WORLD_SIZE:-${PMI_SIZE:-1}}
app=$(readlink -f "./main${BOX3D_DIM:-3}d.gnu.TPROF.MPI.CUDA.ex")
strace -ff -o io_trace -e trace=write,writev,pwrite64,fsync,openat -e status=failed -s 0 -yy "$app" "$@"
code=$?
printf '{"rank":%s,"size":%s,"exit_code":%s}\n' "$rank" "$size" "$code" > "rank_exit_${rank}.json.tmp"
mv "rank_exit_${rank}.json.tmp" "rank_exit_${rank}.json"
exit "$code"
