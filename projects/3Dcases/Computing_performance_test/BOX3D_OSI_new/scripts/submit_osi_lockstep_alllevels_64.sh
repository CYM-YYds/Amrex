#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_osi_lockstep_alllevels_64
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-osi-lockstep-alllevels-64.log
#DSUB -e logs/submit/%J-osi-lockstep-alllevels-64.log
#DSUB -l cuda122
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${CASE_DIR}"
source "${SCRIPT_DIR}/osi_lockstep_args.sh"
osi_lockstep_args 64 true 32
exec "${SCRIPT_DIR}/submit.sh"
