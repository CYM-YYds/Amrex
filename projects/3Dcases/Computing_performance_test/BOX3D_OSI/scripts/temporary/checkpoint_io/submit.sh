#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n checkpoint_io
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=8;mem=16384;gpu=1
#DSUB -N 1
#DSUB -l cuda122
# 本轮临时独立I/O诊断；保留输入、源和日志供复现。
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
CASE=$(cd "$(dirname "$0")/../../.." && pwd)
OUT="$CASE/runs/temporary/checkpoint_io/job${CCS_JOB_ID:?}"
mkdir "$OUT"
cp "$0" "$OUT/submit.sh"
cp "$CASE/scripts/temporary/checkpoint_io/probe.cu" "$OUT/probe.cu"
hostname > "$OUT/node.txt"
nvidia-smi > "$OUT/gpu.txt"
mount | grep whcs-share47 > "$OUT/mount.txt" || true
nvcc -O2 "$OUT/probe.cu" -o "$OUT/probe.ex" > "$OUT/compile.log" 2>&1
cd "$OUT"
strace -o syscalls.log -e trace=write,writev,pwrite64,fsync -s 0 ./probe.ex > result.log 2>&1
