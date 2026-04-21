#!/bin/bash
# DSUB headers must be in the script passed to dsub; keep them here
#DSUB --job_type cosched
#DSUB -n case_Cylinderflow
#DSUB -A root.huazkjdxmrsgjzdsyshi
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-out.log
#DSUB -e logs/submit/%J-out.log
#DSUB -l cuda122

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SHARED_DIR="/home/huazkjdxmrsgjzdsyshi/whcs-share18/caiyimin/learnamerx/Amrex/scripts"
exec "${SHARED_DIR}/submit.sh" "$@"