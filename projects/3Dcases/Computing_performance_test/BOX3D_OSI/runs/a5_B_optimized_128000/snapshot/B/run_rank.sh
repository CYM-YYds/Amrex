#!/usr/bin/env bash
# 为实际应用返回留证据，区分应用未退出与 MPI 启动器收尾未退出。
set -uo pipefail
rank=${OMPI_COMM_WORLD_RANK:-${PMI_RANK:-0}}
size=${OMPI_COMM_WORLD_SIZE:-${PMI_SIZE:-1}}
app=$(readlink -f "./main${BOX3D_DIM:-3}d.gnu.TPROF.MPI.CUDA.ex")
# 设备总用量按1秒采样，是采样峰值而非精确分配高水位；Arena高水位另见应用日志。
nvidia-smi --query-gpu=timestamp,index,uuid,memory.used,memory.total --format=csv -l 1 > gpu_memory_samples.csv 2> gpu_memory_samples.stderr &
monitor_pid=$!
trap 'kill "$monitor_pid" 2>/dev/null || true' EXIT
"$app" "$@"
code=$?
printf '{"rank":%s,"size":%s,"exit_code":%s}\n' "$rank" "$size" "$code" > "rank_exit_${rank}.json.tmp"
mv "rank_exit_${rank}.json.tmp" "rank_exit_${rank}.json"
exit "$code"
