#!/bin/bash
# 代理脚本：转发到 Cylindertest/scripts/compile.sh
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SHARED_DIR="${SCRIPT_DIR}/../../scripts"
#exec "${SHARED_DIR}/compile.sh" "$@"
exec "/home/huazkjdxmrsgjzdsyshi/whcs-share18/caiyimin/learnamerx/Amrex/scripts/compile.sh" "$@"
