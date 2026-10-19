#!/usr/bin/env bash
# GUI 服务只允许在编译节点启动；此构建没有 MPI，使用单进程。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
if [[ "$(hostname -s)" != whshare-agent-1 ]]; then
    echo 'ERROR: pvserver must run on whshare-agent-1; refusing this node.' >&2
    exit 1
fi
PVSERVER="${REPO_DIR}/.tools/paraview-batch/env/bin/pvserver"
if [[ ! -x "$PVSERVER" ]]; then
    echo 'ERROR: install the pinned ParaView environment using install_paraview_batch.sh first.' >&2
    exit 1
fi
# 不继承系统 MPI、Python 或图形插件路径，避免混用旧版模块依赖。
exec env -u LD_LIBRARY_PATH -u PYTHONPATH -u PYTHONHOME -u DISPLAY \
    -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH -u PV_PLUGIN_PATH \
    QT_QPA_PLATFORM=offscreen "$PVSERVER" --force-offscreen-rendering "$@"
