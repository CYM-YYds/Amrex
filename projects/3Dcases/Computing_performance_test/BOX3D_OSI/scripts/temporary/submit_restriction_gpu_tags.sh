#!/usr/bin/env bash
# GPU 建表候选临时验证；保留本入口用于结果复现。
#DSUB --job_type cosched
#DSUB -n restriction_gpu_tags
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=16;mem=65536;gpu=2
#DSUB -N 1
#DSUB -o runs/temporary/restriction_gpu_tags/%J-submit.log
#DSUB -e runs/temporary/restriction_gpu_tags/%J-submit.log
#DSUB -l cuda122
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
CASE="${REGRID_CASE_DIR:?}"
MODE="${RESTRICTION_MODE:-benchmark}"
case "$MODE" in
 benchmark) ROUND=round2 ;;
 integrated) ROUND=integration ;;
 *) echo "错误: RESTRICTION_MODE 只支持 benchmark 或 integrated" >&2; exit 2 ;;
esac
TASK="$CASE/runs/temporary/restriction_gpu_tags/$ROUND"
OUT="$TASK/job${CCS_JOB_ID:?}"
mkdir "$OUT"
printf '%s\n' "$MODE" > "$OUT/mode.txt"
source "$CASE/scripts/osi_experiment_common.sh"
osi_write_hostfile "$OUT/hostfile"
cp "$TASK/main3d.gnu.FLOAT.TPROF.MPI.CUDA.ex" "$OUT/candidate.ex"
cp -a "$TASK/src" "$OUT/src"
cp "$TASK/config/inputs" "$OUT/inputs"
cp "$0" "$OUT/submit.sh"
sha256sum "$OUT/candidate.ex" > "$OUT/executable.sha256"
nvidia-smi > "$OUT/gpu.txt"
cat > "$OUT/rank.sh" <<'RANK'
#!/usr/bin/env bash
set -euo pipefail
exec "$(dirname "$0")/candidate.ex" "$@" > "rank${OMPI_COMM_WORLD_RANK:?}.log" 2>&1
RANK
chmod +x "$OUT/rank.sh"
printf 'case\texit_code\n' > "$OUT/status.tsv"
common=(stop_time=100000 lbm.stream_mode=1 lbm.interp_mode=0 amr.plot_int=-1
 checkpoint.write_particles=false checkpoint.keep_latest_only=false
 verification.osi_ab_check=false verification.final_state_check=false
 verification.convergence_enabled=false verification.refinement_diagnostics=false
 performance.osi_detail_timing=0)
run() {
 local name=$1; shift
 mkdir "$OUT/$name"; cd "$OUT/$name"
 osi_record_command command.txt "$OUT/candidate.ex" "$OUT/inputs" "${common[@]}" "$@"
 local code=0
 osi_launch_mpi hmpi "$OUT/hostfile" 2 2 "$OUT/rank.sh" "$OUT/inputs" "${common[@]}" "$@" > run.log 2>&1 || code=$?
 printf '%s\t%s\n' "$name" "$code" >> "$OUT/status.tsv"
 [[ "$code" == 0 ]]
}
small=(max_step=192 amr.n_cell=32 32 32 amr.max_level=2 amr.max_grid_size=16 16 16
 amr.blocking_factor_x=8 8 8 amr.blocking_factor_y=8 8 8 amr.blocking_factor_z=8 8 8
 amr.n_proper=2 amr.regrid_int=32 geometry.is_periodic=1 1 1
 verification.regrid_fixture=true verification.osi_seed_pattern=true
 checkpoint.begin_step=0 checkpoint.chk_int=192 performance.report_int=192)
if [[ "$MODE" == benchmark ]]; then
 run regression_cpu "${small[@]}" benchmark.gpu_restriction_tags=false
 run regression_gpu "${small[@]}" benchmark.gpu_restriction_tags=true \
  verification.ddf_reference_checkpoint="$OUT/regression_cpu/chk00000192"
else
 # 冻结的双 rank CPU 基线只读复用，正式程序已使用 GPU 建表。
 ln -s "$CASE/runs/temporary/restriction_gpu_tags/round2/job623333/regression_cpu" "$OUT/regression_cpu"
 run regression_gpu "${small[@]}" \
  verification.ddf_reference_checkpoint="$OUT/regression_cpu/chk00000192"
fi
"$CASE/../../../../.venv/bin/python" "$CASE/scripts/temporary/analyze_restriction_gpu_tags.py" "$OUT" --regression-only
[[ "$MODE" == benchmark ]] || exit 0
native=(amr.n_cell=128 128 128 amr.max_level=2 amr.max_grid_size=128 256 512
 amr.blocking_factor_x=16 16 16 amr.blocking_factor_y=16 16 16 amr.blocking_factor_z=16 16 16
 amr.n_proper=1 amr.regrid_int=32 lbm.err=0.125 0.25 0.5 1 checkpoint.begin_step=128000
 checkpoint.chk_prefix="$CASE/runs/a5_B_optimized_128000/job622137/B/chk"
 checkpoint.chk_int=-1 max_step=128640 performance.report_int=160)
sequence=(false true true false false true true false)
for detail in false; do
 for i in "${!sequence[@]}"; do
  run "timing_${detail}_${i}_${sequence[$i]}" "${native[@]}" \
   benchmark.gpu_restriction_tags="${sequence[$i]}" performance.regrid_detail_timing="$detail"
 done
done
