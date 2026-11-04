#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n C_AB_IO_paths
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -l cuda122
# 同节点三条真实输出路径诊断；源码、命令、输出及跟踪留作复现材料。
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
CASE=$(cd "$(dirname "$0")/../../.." && pwd)
exec "$CASE/../../../../.venv/bin/python" "$CASE/scripts/temporary/checkpoint_io/paired_paths.py"
