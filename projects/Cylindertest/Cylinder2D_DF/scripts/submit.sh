#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SHARED_DIR="${SCRIPT_DIR}/../scripts"
#exec "${SHARED_DIR}/submit.sh" "$@"
exec "/home/huazkjdxmrsgjzdsyshi/whcs-share18/caiyimin/learnamerx/Amrex/scripts/submit.sh" "$@"