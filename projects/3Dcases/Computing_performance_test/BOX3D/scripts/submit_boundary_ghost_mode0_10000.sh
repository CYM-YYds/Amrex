#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_boundary_mode0_10000
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-boundary-mode0-10000.log
#DSUB -e logs/submit/%J-boundary-mode0-10000.log
#DSUB -l cuda122

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SHARED_DIR="/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/scripts"

cd "${CASE_DIR}"
export AMREX_RUN_ARGS="max_step=10000 lbm.boundary_ghost_mode=0 amr.plot_file=data_boundary_mode0_10000/case_Re1_4p_ checkpoint.chk_int=-1"
exec "${SHARED_DIR}/submit.sh"
