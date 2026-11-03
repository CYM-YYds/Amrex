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
ROOT="${IO_OUTPUT_ROOT:-$CASE/runs/temporary/checkpoint_io}"
mkdir -p "$ROOT"
OUT="$ROOT/job${CCS_JOB_ID:?}"
mkdir "$OUT"
cp "$0" "$OUT/submit.sh"
cp "$CASE/scripts/temporary/checkpoint_io/probe.cu" "$OUT/probe.cu"
hostname > "$OUT/node.txt"
nvidia-smi > "$OUT/gpu.txt" 2>&1 || true
mount | grep whcs-share47 > "$OUT/mount.txt" || true
if [[ "${IO_ENVIRONMENT_ONLY:-0}" == 1 ]]; then
    df -hT /tmp /var/tmp "$OUT" > "$OUT/filesystems.txt"
    printf 'TMPDIR=%s\n' "${TMPDIR:-}" > "$OUT/environment.txt"
    dmesg --ctime > "$OUT/kernel_raw.log" 2> "$OUT/kernel_access.stderr" || true
    grep -Ei 'dpc|I/O error|input/output|nvme|nfs|ext4.*error|xfs.*error' "$OUT/kernel_raw.log" | tail -80 > "$OUT/storage_kernel.log" || true
    if [[ -n "${IO_INSPECT_PATH:-}" ]]; then
        case "$IO_INSPECT_PATH" in /var/tmp/box512_stage_*|/tmp/box512_stage_*) ;; *) exit 2;; esac
        find "$IO_INSPECT_PATH" -maxdepth 4 -type f -printf '%p %s bytes\n' > "$OUT/local_files.txt"
        for local_log in "$IO_INSPECT_PATH"/*/run.log; do
            [[ -f "$local_log" ]] && tail -25 "$local_log"
        done > "$OUT/local_run_tail.txt" 2>&1 || true
        ps -u "$(id -u)" -o pid,ppid,stat,wchan:24,etime,comm | grep -E 'candidate|strace|mpirun' > "$OUT/process_states.txt" || true
    fi
    exit 0
fi
nvcc -O2 "$OUT/probe.cu" -o "$OUT/probe.ex" > "$OUT/compile.log" 2>&1
cd "$OUT"
read -r -a probe_args <<< "${IO_PROBE_ARGS:-}"
printf '%s\n' "${probe_args[*]}" > probe_args.txt
strace -o syscalls.log -e trace=write,writev,pwrite64,fsync -s 0 ./probe.ex "${probe_args[@]}" > result.log 2>&1
