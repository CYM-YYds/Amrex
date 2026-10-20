#!/usr/bin/env bash
# 使用独立 ParaView 应用环境；不改 shell 启动文件或仓库 Python 环境。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
PVBATCH="${PARAVIEW_BATCH_EXECUTABLE:-${REPO_DIR}/.tools/paraview-mpi/env/bin/pvbatch}"
# ParaView 必须在指定编译节点执行；SSH 失败时停止，不回退到登录节点。
BATCH_HOST="whshare-agent-1"
if [[ "$(hostname -s)" != "${BATCH_HOST%%.*}" ]]; then
    printf -v REMOTE_COMMAND '%q ' env \
        "PARAVIEW_BATCH_EXECUTABLE=${PVBATCH}" bash "${SCRIPT_DIR}/run_paraview_batch.sh" "$@"
    exec env -u LD_LIBRARY_PATH ssh -T -o BatchMode=yes -o ConnectTimeout=10 \
        "${BATCH_HOST}" "${REMOTE_COMMAND}"
fi
if [[ ! -x "${PVBATCH}" ]]; then
    echo "ERROR: pvbatch not found: ${PVBATCH}; run .agents/skills/paraview/scripts/install_paraview_batch.sh first" >&2
    exit 1
fi
exec env -u LD_LIBRARY_PATH -u PYTHONPATH -u PYTHONHOME -u DISPLAY \
    -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH QT_QPA_PLATFORM=offscreen \
    "${PVBATCH}" --force-offscreen-rendering "${SCRIPT_DIR}/paraview_batch_export.py" "$@"
