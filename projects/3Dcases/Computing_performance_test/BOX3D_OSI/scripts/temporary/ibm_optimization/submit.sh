#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n ibm_weights_compare
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=8;mem=32768;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-ibm-weights.log
#DSUB -e logs/submit/%J-ibm-weights.log
#DSUB -l cuda122
# 本任务重复对照入口，保留以复现性能与状态检查。
set -euo pipefail
CASE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
TASK="${IBM_TASK_DIR:-$CASE/runs/temporary/ibm_optimization}"
NPROCS="${IBM_NPROCS:-1}"
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
source "$CASE/scripts/osi_experiment_common.sh"
HOSTFILE="$TASK/hostfile.${CCS_JOB_ID:-$$}"
osi_write_hostfile "$HOSTFILE"
nvidia-smi > "$TASK/gpu.${CCS_JOB_ID:-$$}.txt"
if [[ "${IBM_STAGE:-performance}" == validation ]]; then
  check(){
    local name=$1 dim=$2; shift 2
    local dest="$TASK/${name}_${NPROCS}gpu${IBM_VALIDATION_TAG:-}"
    [[ ! -e "$dest" ]] || exit 2
    mkdir -p "$dest"
    cp "$TASK/inputs" "$dest/inputs"
    cd "$dest"
    local cmd=("$TASK/app${dim}d.ex" inputs checkpoint.begin_step=0 max_step=32
      amr.write_initial=false amr.plot_int=-1 checkpoint.chk_int=32 performance.report_int=32
      ibm.verify_local_sum=true 'amr.n_error_buf=0 0 0 0' amr.blocking_factor=8
      'amr.blocking_factor_x=8 8 8 8' 'amr.blocking_factor_y=8 8 8 8' 'amr.blocking_factor_z=8 8 8 8'
      'amr.max_grid_size=16 16 16 16' amr.max_level=1 amr.regrid_int=10
      "${@}")
    osi_record_command command.txt "${cmd[@]}"
    mpirun -hostfile "$HOSTFILE" -n "$NPROCS" -npernode "$NPROCS" -x PATH -x LD_LIBRARY_PATH \
      --mca plm_rsh_agent /opt/batch/agent/tools/dstart env -u BASH_ENV bash --noprofile --norc \
      -c 'exec "$@"' bash "${cmd[@]}" > run.log 2>&1
    touch completed.txt
  }
  check dynamic2d 2 'amr.n_cell=128 128' ibm.radius=0.04
  check periodic2d 2 'amr.n_cell=64 64' amr.max_level=0 lbm.flow_boundary=cavity \
    'geometry.is_periodic=1 0' 'ibm.center=0.5 0.8' ibm.radius=0.06
  check dynamic3d 3 'geometry.prob_lo=0 0 0' 'geometry.prob_hi=1 1 1' \
    'geometry.is_periodic=0 0 0' 'amr.n_cell=32 32 32' 'ibm.center=0.5 0.5 0.5' \
    particles.shape=sphere ibm.radius=0.12 lbm.flow_boundary=cavity
  check periodic3d 3 'geometry.prob_lo=0 0 0' 'geometry.prob_hi=1 1 1' \
    'geometry.is_periodic=1 0 1' 'amr.n_cell=32 32 32' 'ibm.center=0.5 0.65 0.5' \
    particles.shape=sphere ibm.radius=0.1 amr.max_level=0 lbm.flow_boundary=cavity
  dest="$TASK/full_dynamic_${NPROCS}gpu${IBM_VALIDATION_TAG:-}"
  [[ ! -e "$dest" ]] || exit 2
  mkdir -p "$dest"
  cp "$TASK/inputs" "$dest/inputs"
  ln -s "$CASE/runs/box_cylinder_native_520000/production/chk00584000" "$dest/chk00584000"
  cd "$dest"
  cmd=("$TASK/app2d.ex" inputs checkpoint.begin_step=584000 max_step=584064
    amr.write_initial=false amr.plot_int=-1 checkpoint.chk_int=64 performance.report_int=64
    ibm.verify_local_sum=true)
  osi_record_command command.txt "${cmd[@]}"
  mpirun -hostfile "$HOSTFILE" -n "$NPROCS" -npernode "$NPROCS" -x PATH -x LD_LIBRARY_PATH \
    --mca plm_rsh_agent /opt/batch/agent/tools/dstart env -u BASH_ENV bash --noprofile --norc \
    -c 'exec "$@"' bash "${cmd[@]}" > run.log 2>&1
  touch completed.txt
  exit 0
fi
for pair in 1 2 3; do
  order=(baseline optimized)
  if [[ $pair == 2 ]]; then order=(optimized baseline); fi
  for variant in "${order[@]}"; do
    dest="$TASK/${variant}_${pair}"
    [[ ! -e "$dest" ]] || exit 2
    mkdir -p "$dest"
    cp "$TASK/inputs" "$dest/inputs"
    ln -s "$CASE/runs/box_cylinder_native_520000/production/chk00584000" "$dest/chk00584000"
    cd "$dest"
    cmd=("$TASK/$variant.ex" inputs checkpoint.begin_step=584000 max_step=584032 amr.regrid_int=-1 amr.write_initial=false amr.plot_int=-1 checkpoint.chk_int=32 performance.report_int=32)
    osi_record_command command.txt "${cmd[@]}"
    mpirun -hostfile "$HOSTFILE" -n "$NPROCS" -npernode "$NPROCS" -x PATH -x LD_LIBRARY_PATH \
      --mca plm_rsh_agent /opt/batch/agent/tools/dstart env -u BASH_ENV bash --noprofile --norc \
      -c 'exec "$@"' bash "${cmd[@]}" > run.log 2>&1
    touch completed.txt
  done
done
