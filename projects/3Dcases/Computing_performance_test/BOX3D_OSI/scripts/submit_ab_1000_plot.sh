#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_ab_1000_plot
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-ab-1000-plot.log
#DSUB -e logs/submit/%J-ab-1000-plot.log
#DSUB -l cuda122

export AB_MAX_STEP=1000
export AB_PLOT_INT=100
export AB_CHK_INT=1000
export AB_RESTART_STEP=223
export AB_RESTART_PREFIX=/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D/tmp/ab_four_level_smoke_585946/chk
exec "$(dirname "$0")/submit_ab_four_level_smoke.sh"
