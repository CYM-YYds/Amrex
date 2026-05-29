#!/bin/bash
# 代理脚本：转发到仓库级 compile.sh，并将 BOX3D 编译日志放入独立目录。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SHARED_DIR="${SCRIPT_DIR}/../../scripts"
LOG_DIR="${LOG_DIR:-${CASE_DIR}/logs/compile}"
export LOG_DIR
#exec "${SHARED_DIR}/compile.sh" "$@"
exec "/home/huazkjdxmrsgjzdsyshi/whcs-share18/caiyimin/learnamerx/Amrex/scripts/compile.sh" "$@"
