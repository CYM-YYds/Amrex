#!/bin/bash
set -euo pipefail

CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

compile_script="${CASE_DIR}/scripts/compile.sh"
submit_script="${CASE_DIR}/scripts/submit.sh"
submit_perf_script="${CASE_DIR}/scripts/submit_perf.sh"

grep -F 'LOG_DIR="${LOG_DIR:-${CASE_DIR}/logs/compile}"' "${compile_script}" >/dev/null
grep -F '#DSUB -o logs/submit/%J-out.log' "${submit_script}" >/dev/null
grep -F '#DSUB -e logs/submit/%J-out.log' "${submit_script}" >/dev/null
grep -F '#DSUB -o logs/submit/%J-out.log' "${submit_perf_script}" >/dev/null
grep -F '#DSUB -e logs/submit/%J-out.log' "${submit_perf_script}" >/dev/null

test -d "${CASE_DIR}/logs/compile"
test -d "${CASE_DIR}/logs/submit"
