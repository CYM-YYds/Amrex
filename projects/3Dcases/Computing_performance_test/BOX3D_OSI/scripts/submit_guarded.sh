#!/usr/bin/env bash
# 提交节点常驻监督不依赖计算节点的 shell 和日志转发进程。
set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
CASE_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)
REPO_DIR=$(git -C "$CASE_DIR" rev-parse --show-toplevel)
cd "$CASE_DIR"
export CCS_CLI_HOME=${CCS_CLI_HOME:-/opt/batch/cli}
mkdir -p logs/submit
reply=$(dsub "$@" -s "${SCRIPT_DIR}/submit.sh")
printf '%s\n' "$reply"
job=$(awk '$1 ~ /^[0-9]+$/ {print $1; exit}' <<< "$reply")
if [[ -z "$job" ]]; then
    echo 'ERROR: 无法解析作业编号，未启动监督器' >&2
    exit 1
fi
# 监督器脱离终端，终端关闭后仍可回收这个确定的作业编号。
nohup setsid -f "${REPO_DIR}/.venv/bin/python" "${SCRIPT_DIR}/watch_job.py" \
    --job "$job" --case "$CASE_DIR" \
    --grace-seconds "${BOX3D_SCHEDULER_EXIT_GRACE_SECONDS:-60}" \
    > "logs/submit/${job}-supervisor.log" 2>&1 < /dev/null
for ((attempt=0; attempt<50; attempt++)); do
    if [[ -s "logs/submit/${job}-supervisor.pid" ]]; then
        echo "作业 ${job} 的提交节点监督器已启动"
        exit 0
    fi
    sleep 0.1
done
echo "ERROR: 监督器未启动，终止刚提交的作业 ${job}" >&2
djob -T "$job"
exit 1
