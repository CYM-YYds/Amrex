#!/bin/bash
#DSUB --job_type cosched
#DSUB -n box3d_re1000_128k_ab
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-re1000-128k-ab.log
#DSUB -e logs/submit/%J-re1000-128k-ab.log
#DSUB -l cuda122

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 标量参数在命令行锁定，数组阈值由运行目录中的 inputs 快照保存；
# MLUPS 按各窗口实际完成的 AMR 加权更新量计算。
export AMREX_RUN_ARGS="${AMREX_RUN_ARGS:-} \
max_step=128000 \
lbm.reynolds_number=1000.0 \
lbm.stream_mode=0 \
amr.max_level=2 \
amr.regrid_int=32 \
amr.plot_int=-1 \
performance.report_int=3200 \
checkpoint.begin_step=0 \
checkpoint.chk_int=32000 \
checkpoint.chk_prefix=chk \
checkpoint.keep_latest_only=true \
checkpoint.write_particles=false \
verification.convergence_enabled=false \
verification.osi_seed_pattern=false \
verification.osi_ab_check=false"

exec "${SCRIPT_DIR}/submit.sh"
