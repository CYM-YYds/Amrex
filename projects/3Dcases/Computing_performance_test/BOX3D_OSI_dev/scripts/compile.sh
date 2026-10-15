#!/usr/bin/env bash
# 算例维度在本入口选择，公共脚本负责环境加载、远程构建和数据库采集。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_DIR="$(git -C "$CASE_DIR" rev-parse --show-toplevel)"
DIM="${DIM:-3}"
[[ "$DIM" == 2 || "$DIM" == 3 ]] || { echo "错误: DIM 必须为 2 或 3" >&2; exit 1; }
export AMREX_BUILD_DIM="$DIM"
export LOG_DIR="${LOG_DIR:-${CASE_DIR}/logs/compile}"
# 只合并完整且维度相同的数据库；切换维度或缺少源码记录时重新采集全部算例源文件。
if [[ "${GEN_CCDB:-1}" == 1 ]]; then
    export CCDB_REQUIRE_FULL=1
    if "${REPO_DIR}/.venv/bin/python" - "$CASE_DIR" "$DIM" <<'PYCODE'
import json
from pathlib import Path
import sys
case, dimension = Path(sys.argv[1]), sys.argv[2]
try:
    rows = json.loads((case / 'config/compile_commands.json').read_text())
    actual = {Path(row['file']).name for row in rows}
    expected = {p.name for p in (case / 'src').glob('*.cpp')}
    if dimension == '2':
        expected -= {'LagrangeParticleContainer.cpp', 'AuxiliaryPointContainer.cpp'}
    valid = expected <= actual and all(
        f'-DAMREX_SPACEDIM={dimension}' in row.get('arguments', row.get('command', '').split())
        for row in rows)
except (OSError, ValueError, KeyError):
    valid = False
sys.exit(0 if valid else 1)
PYCODE
    then
        export CCDB_MERGE_OLD=1 CCDB_REQUIRE_FULL=0
    else
        export CCDB_MERGE_OLD=0
        find "$CASE_DIR/src" -maxdepth 1 -name '*.cpp' -exec touch -- {} +
    fi
fi
cd "$CASE_DIR"
exec "${REPO_DIR}/scripts/compile.sh" "$@"
