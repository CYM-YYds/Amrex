#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n C_optimized_t1000
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -l cuda122
#DSUB -o logs/submit/%J-C-optimized.log
#DSUB -e logs/submit/%J-C-optimized.log
# 本次正式重跑的临时编排入口；源码、流场及复现证据长期保留。
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
CASE=$(cd "$(dirname "$0")/../../.." && pwd)
REPO=$(git -C "$CASE" rev-parse --show-toplevel)
exec "$REPO/.venv/bin/python" "$CASE/scripts/temporary/C_optimized_t1000/run.py"
