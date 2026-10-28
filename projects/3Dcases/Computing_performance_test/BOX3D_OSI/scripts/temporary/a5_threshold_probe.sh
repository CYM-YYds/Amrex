#!/usr/bin/env bash
# A5 对标前的阈值初测；本任务结束且不再需要复现时可删除，数据与报告保留。
#DSUB --job_type cosched
#DSUB -n a5_threshold_probe
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-a5-threshold.log
#DSUB -e logs/submit/%J-a5-threshold.log
#DSUB -l cuda122
set -euo pipefail
CASE=$(cd "$(dirname "$0")/../.." && pwd)
REPO=$(git -C "$CASE" rev-parse --show-toplevel)
OUT="$CASE/runs/temporary/a5_threshold_probe/job${CCS_JOB_ID:?}"
mkdir -p "$OUT"
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
cp "$CASE/config/inputs" "$OUT/inputs"
cp -a "$CASE/src" "$OUT/src"
cp -a "$CASE/config" "$OUT/config"
cp "$CASE/main3d.gnu.FLOAT.TPROF.MPI.CUDA.ex" "$OUT/application.ex"
cp "$0" "$OUT/submit.sh"
git -C "$CASE" rev-parse HEAD > "$OUT/git_head.txt"
git -C "$CASE" diff > "$OUT/source.diff"
sha256sum "$OUT/application.ex" "$OUT/inputs" > "$OUT/fingerprints.sha256"
nvidia-smi > "$OUT/gpu.txt"
awk 'NF >= 2 {print $1 " slots=" $2}' "${CCS_ALLOC_FILE:?}" > "$OUT/hostfile"
printf 'case\texit_code\n' > "$OUT/status.tsv"
common=(max_step=3200 stop_time=25
    lbm.reynolds_number=1000 lbm.stream_mode=1 lbm.interp_mode=0
    amr.n_cell=128 128 128 amr.max_level=2 amr.regrid_int=32
    amr.max_grid_size=128 128 128
    amr.blocking_factor_x=16 16 16 amr.blocking_factor_y=16 16 16 amr.blocking_factor_z=16 16 16
    amr.n_error_buf=1 1 1 amr.grid_eff=0.7
    amr.plot_int=-1 checkpoint.begin_step=0 checkpoint.chk_int=3200
    checkpoint.chk_prefix=chk checkpoint.keep_latest_only=false
    verification.convergence_enabled=false verification.refinement_diagnostics=true
    verification.regrid_valid_check=true verification.final_state_check=true
    performance.report_int=320 performance.osi_detail_timing=0)
failed=0
for name in A B C; do
    case "$name" in
        A) thresholds=(0.0625 0.125 0.25 0.5);;
        B) thresholds=(0.125 0.25 0.5 1);;
        C) thresholds=(0.25 0.5 1 2);;
    esac
    mkdir "$OUT/$name"
    cd "$OUT/$name"
    ln -s ../application.ex main3d.gnu.TPROF.MPI.CUDA.ex
    cp "$CASE/scripts/run_rank.sh" run_rank.sh
    args=("${common[@]}" lbm.err="${thresholds[0]}" "${thresholds[@]:1}")
    printf '%q ' "$OUT/application.ex" "$OUT/inputs" "${args[@]}" > command.txt
    printf '\n' >> command.txt
    code=0
    "$REPO/.venv/bin/python" "$CASE/scripts/guard_run.py" --wall-seconds 1800 -- \
        mpirun -hostfile "$OUT/hostfile" -n 1 -npernode 1 -x PATH -x LD_LIBRARY_PATH \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
        env -u BASH_ENV bash --noprofile --norc -c \
        'export CUDA_VISIBLE_DEVICES=${OMPI_COMM_WORLD_LOCAL_RANK:-0}; exec bash ./run_rank.sh "$@"' \
        bash "$OUT/inputs" "${args[@]}" || code=$?
    printf '%s\t%s\n' "$name" "$code" >> "$OUT/status.tsv"
    echo "case=$name exit=$code output=$OUT/$name"
    if (( code != 0 )); then failed=1; fi
done
exit "$failed"
