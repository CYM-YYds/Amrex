#!/bin/bash
set -euo pipefail

CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

submit_script="${CASE_DIR}/scripts/submit.sh"

grep -F '#DSUB -o logs/submit/%J-out.log' "${submit_script}" >/dev/null
grep -F '#DSUB -e logs/submit/%J-out.log' "${submit_script}" >/dev/null

test -d "${CASE_DIR}/logs/submit"
