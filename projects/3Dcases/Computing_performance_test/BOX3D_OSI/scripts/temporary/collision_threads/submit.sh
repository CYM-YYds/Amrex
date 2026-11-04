#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n collision_threads
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=16;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o runs/temporary/collision_threads/round1/%J-submit.log
#DSUB -e runs/temporary/collision_threads/round1/%J-submit.log
#DSUB -l cuda122
# 本任务临时入口；保留与冻结实验对应，任务废弃后可删除。
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
REPO_DIR="$(git -C "$CASE_DIR" rev-parse --show-toplevel)"
exec "$REPO_DIR/.venv/bin/python" "$CASE_DIR/scripts/temporary/collision_threads/experiment.py" "${THREAD_SCAN_ACTION:-run}"
