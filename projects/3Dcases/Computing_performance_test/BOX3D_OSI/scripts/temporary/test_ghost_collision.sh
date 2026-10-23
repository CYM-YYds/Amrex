#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n ghost_collision
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=2
#DSUB -N 1
#DSUB -o logs/submit/%J-ghost.log
#DSUB -e logs/submit/%J-ghost.log
#DSUB -l cuda122
# 本次 ghost 碰撞实验的复现入口，结果留存期间保留。
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EXP="$CASE_DIR/runs/ghost_collision_20261023"
if [[ "${COLLISION_TEST:-0}" == 1 ]]; then EXP="$CASE_DIR/runs/collision_batch_20261023"; fi
if [[ "${COLLISION_COORD_TEST:-0}" == 1 ]]; then EXP="$CASE_DIR/runs/collision_coordinates_20261023"; fi
OUT="$EXP/job${CCS_JOB_ID:?}"
mkdir -p "$OUT"
cp "$EXP/candidate/main3d.gnu.TPROF.MPI.CUDA.ex" "$OUT/app.ex"
cp "$EXP/candidate/config/inputs" "$OUT/inputs"
cp "$0" "$OUT/submit.sh"
sha256sum "$OUT/app.ex" > "$OUT/executable.sha256"
awk 'NF >= 2 {print $1 " slots=" $2}' "${CCS_ALLOC_FILE:?}" > "$OUT/hostfile"
nvidia-smi > "$OUT/gpu.txt"
printf '%s\n' "${GHOST_TEST_MODE:-all}" > "$OUT/mode.txt"
printf 'case\texit\n' > "$OUT/status.tsv"
run() {
 local name=$1 ranks=$2; shift 2
 if [[ "${COLLISION_COORD_TEST:-0}" == 1 ]]; then
  local cached=false
  [[ "$name" == *b1* ]] && cached=true
  set -- "$@" lbm.collision_batch_mode=0 lbm.collision_cached_coordinates="$cached"
 fi
 mkdir -p "$OUT/$name"; cd "$OUT/$name"
 printf '%q ' "$OUT/app.ex" "$OUT/inputs" "$@" > command.txt
 local code=0
 mpirun -hostfile "$OUT/hostfile" -n "$ranks" -npernode "$ranks" -x PATH -x LD_LIBRARY_PATH \
 --mca plm_rsh_agent /opt/batch/agent/tools/dstart /usr/bin/env -u BASH_ENV \
 bash --noprofile --norc -c 'export CUDA_VISIBLE_DEVICES=${OMPI_COMM_WORLD_LOCAL_RANK:-0}; exec "$@"' bash \
 "$OUT/app.ex" "$OUT/inputs" "$@" > run.log 2>&1 || code=$?
 printf '%s\t%s\n' "$name" "$code" >> "$OUT/status.tsv"
 [[ "$code" == 0 ]]
}
common=(amr.plot_int=-1 checkpoint.chk_int=-1 verification.convergence_enabled=false
 performance.osi_detail_timing=0 lbm.stream_mode=1 lbm.collide_mode=1)
checks=(verification.osi_ab_check=true verification.osi_ab_continue_on_mismatch=false
 verification.osi_step_entry_check_interval=1 verification.osi_check_after_average_valid=true
 verification.check_after_first_fill=true lbm.skip_same_level_ghost_collision=true)
mesh=(amr.n_cell=32 32 32 amr.max_level=2 amr.max_grid_size=16 16 16
 amr.blocking_factor_x=8 8 8 amr.blocking_factor_y=8 8 8 amr.blocking_factor_z=8 8 8 amr.n_proper=2)
# 批量碰撞实验复用相同运行与溯源入口，避免复制提交框架。
if [[ "${COLLISION_TEST:-0}" == 1 ]]; then
 check_variants=(1 2); real_variants=(0 1 2); timing_variants=(0 1 2 2 1 0)
 if [[ "${COLLISION_COORD_TEST:-0}" == 1 ]]; then
  check_variants=(1); real_variants=(0 1); timing_variants=(0 1 1 0)
 fi
 if [[ "${GHOST_TEST_MODE:-all}" != timing ]]; then
  for variant in "${check_variants[@]}"; do
   for ranks in 1 2; do
    for mode in 0 1 2; do
     for boundary in cavity periodic; do
      domain=(0 0 0); seed=false
      if [[ "$boundary" == periodic ]]; then domain=(1 1 1); seed=true; fi
      run "check_b${variant}_r${ranks}_i${mode}_${boundary}" "$ranks" "${common[@]}" "${checks[@]}" "${mesh[@]}" \
       lbm.collision_batch_mode="$variant" max_step=64 amr.regrid_int=16 verification.regrid_fixture=true \
       performance.report_int=16 lbm.interp_mode="$mode" geometry.is_periodic="${domain[@]}" verification.osi_seed_pattern="$seed"
     done
    done
   done
  done
 fi
 [[ "${GHOST_TEST_MODE:-all}" == checks ]] && exit 0
 restart="$CASE_DIR/runs/20261019_124153_job615364/chk00256000"
 prod=("${common[@]}" checkpoint.begin_step=256000 checkpoint.chk_prefix="$restart/../chk"
  amr.max_level=2 amr.regrid_int=-1 performance.report_int=160
  amr.blocking_factor_x=32 32 32 amr.blocking_factor_y=32 32 32 amr.blocking_factor_z=32 32 32)
 if [[ "${GHOST_TEST_MODE:-all}" != timing ]]; then
  # 参考态在本地输出，不允许 checkpoint 清理历史输入目录。
  for variant in "${real_variants[@]}"; do
   name="real_b${variant}"
   mkdir -p "$OUT/$name"
   ln -s "$restart" "$OUT/$name/chk00256000"
   extra=()
   if [[ "$variant" != 0 ]]; then extra+=("verification.ddf_reference_checkpoint=$OUT/real_b0/chk00256032"); fi
   run "$name" 1 "${prod[@]}" checkpoint.chk_prefix=chk checkpoint.keep_latest_only=false \
    checkpoint.chk_int=32 max_step=256032 lbm.collision_batch_mode="$variant" "${extra[@]}"
  done
 fi
 for variant in "${timing_variants[@]}"; do
  run "timing_b${variant}_$(date +%s)" 1 "${prod[@]}" max_step=256640 lbm.collision_batch_mode="$variant"
 done
 exit 0
fi
if [[ "${GHOST_TEST_MODE:-all}" != timing ]]; then
ranks_list=(1 2); boundary_list=(cavity periodic)
if [[ "${GHOST_TEST_MODE:-all}" == control ]]; then
 ranks_list=(2); boundary_list=(cavity)
 checks+=(lbm.skip_same_level_ghost_collision=false)
fi
for ranks in "${ranks_list[@]}"; do
 for mode in 0 1 2; do
  for boundary in "${boundary_list[@]}"; do
   domain=(0 0 0); seed=false
   if [[ "$boundary" == periodic ]]; then domain=(1 1 1); seed=true; fi
   run "check_r${ranks}_i${mode}_${boundary}" "$ranks" "${common[@]}" "${checks[@]}" "${mesh[@]}" \
    max_step=64 amr.regrid_int=16 verification.regrid_fixture=true performance.report_int=32 \
    lbm.interp_mode="$mode" geometry.is_periodic="${domain[@]}" verification.osi_seed_pattern="$seed"
  done
 done
done
fi
[[ "${GHOST_TEST_MODE:-all}" == control ]] && exit 0
# 真实三层 checkpoint，同一节点单卡交替运行；先正确性，再性能。
restart="$CASE_DIR/runs/20261019_124153_job615364/chk00256000"
prod=("${common[@]}" checkpoint.begin_step=256000 checkpoint.chk_prefix="$restart/../chk"
 max_step=256320 amr.max_level=2 amr.regrid_int=-1 performance.report_int=160
 amr.blocking_factor_x=32 32 32 amr.blocking_factor_y=32 32 32 amr.blocking_factor_z=32 32 32)
if [[ "${GHOST_TEST_MODE:-all}" != timing ]]; then
run reference 1 "${prod[@]}" checkpoint.chk_prefix=chk checkpoint.begin_step=0 max_step=64 "${mesh[@]}" \
 verification.regrid_fixture=true checkpoint.chk_int=64 lbm.skip_same_level_ghost_collision=false
run candidate 1 "${prod[@]}" checkpoint.begin_step=0 max_step=64 "${mesh[@]}" \
 verification.regrid_fixture=true lbm.skip_same_level_ghost_collision=true \
 "verification.ddf_reference_checkpoint=$OUT/reference/chk00000064"
fi
for variant in off on on off; do
 enabled=false; [[ "$variant" == on ]] && enabled=true
 name="timing_${variant}_$(date +%s)"
 run "$name" 1 "${prod[@]}" lbm.skip_same_level_ghost_collision="$enabled"
done
