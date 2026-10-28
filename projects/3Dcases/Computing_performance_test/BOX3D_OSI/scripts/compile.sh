#!/usr/bin/env bash
# 算例维度在本入口选择，公共脚本负责环境加载、远程构建和数据库采集。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_DIR="$(git -C "$CASE_DIR" rev-parse --show-toplevel)"
DIM="${DIM:-3}"
[[ "$DIM" == 2 || "$DIM" == 3 ]] || { echo "错误: DIM 必须为 2 或 3" >&2; exit 1; }
PRECISION="${PRECISION:-FLOAT}"
[[ "$PRECISION" == DOUBLE || "$PRECISION" == FLOAT ]] || { echo "错误: PRECISION 必须为 DOUBLE 或 FLOAT" >&2; exit 1; }
export AMREX_BUILD_DIM="$DIM" AMREX_BUILD_PRECISION="$PRECISION"
export LOG_DIR="${LOG_DIR:-${CASE_DIR}/logs/compile}"
# 只合并维度、场精度及当前单精度粒子配置一致且完整的数据库。
if [[ "${GEN_CCDB:-1}" == 1 ]]; then
    export CCDB_REQUIRE_FULL=1
    if "${REPO_DIR}/.venv/bin/python" - "$CASE_DIR" "$DIM" "$PRECISION" <<'PYCODE'
import json
from pathlib import Path
import sys
case, dimension, precision = Path(sys.argv[1]), sys.argv[2], sys.argv[3]
try:
    rows = json.loads((case / 'config/compile_commands.json').read_text())
    actual = {Path(row['file']).name for row in rows}
    expected = {p.name for p in (case / 'src').glob('*.cpp')}
    valid = expected <= actual and all(
        f'-DAMREX_SPACEDIM={dimension}' in row.get('arguments', row.get('command', '').split())
        for row in rows) and all(
        ('-DBL_USE_FLOAT' in row.get('arguments', row.get('command', '').split()))
        == (precision == 'FLOAT') for row in rows) and all(
        '-DAMREX_SINGLE_PRECISION_PARTICLES' in row.get('arguments', row.get('command', '').split())
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
