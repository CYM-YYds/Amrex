#!/usr/bin/env bash
# AI 临时工具：本次双卡续跑的显存采样；任务结束后无后续依赖时可删除。
#DSUB --job_type cosched
#DSUB -n osi_L2_memory
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=64;mem=98304;gpu=2
#DSUB -N 1
#DSUB -o logs/submit/%J-out.log
#DSUB -e logs/submit/%J-out.log
#DSUB -l cuda122
set -euo pipefail
CASE_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$CASE_DIR"
CSV="$CASE_DIR/logs/submit/${CCS_JOB_ID}-gpu-memory.csv"
mkdir -p logs/submit
# 单节点采样全部 GPU；UUID 和编号用于与实际作业进程核对。
stdbuf -oL nvidia-smi --query-gpu=timestamp,index,uuid,memory.used,memory.total --format=csv,noheader,nounits --loop-ms=200 > "$CSV" 2> "${CSV%.csv}.err" &
MONITOR_PID=$!
cleanup() {
    kill "$MONITOR_PID" 2>/dev/null || true
    wait "$MONITOR_PID" 2>/dev/null || true
}
trap cleanup EXIT
bash "$CASE_DIR/scripts/submit.sh"
