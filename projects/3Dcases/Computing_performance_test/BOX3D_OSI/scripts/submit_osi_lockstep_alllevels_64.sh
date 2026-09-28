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
export AMREX_RUN_ARGS="max_step=64 lbm.reynolds_number=1000.0 lbm.stream_mode=1 lbm.osi_local_direct=1 lbm.osi_mpi_direct=1 amr.n_cell=64 64 64 amr.max_level=2 amr.max_grid_size=64 64 64 amr.blocking_factor_x=32 32 32 amr.blocking_factor_y=32 32 32 amr.blocking_factor_z=32 32 32 amr.regrid_int=32 amr.plot_int=-1 performance.report_int=32 checkpoint.chk_int=-1 verification.convergence_enabled=false verification.osi_seed_pattern=false verification.osi_ab_check=true verification.osi_ab_continue_on_mismatch=true verification.osi_step_entry_check_step=64 verification.osi_check_after_average_valid=true verification.check_after_first_fill=true"
exec "${SCRIPT_DIR}/submit.sh"
