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
TASK="$CASE/runs/temporary/ibm_optimization"
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
source "$CASE/scripts/osi_experiment_common.sh"
osi_write_hostfile "$TASK/hostfile"
nvidia-smi > "$TASK/gpu.txt"
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
    mpirun -hostfile "$TASK/hostfile" -n 1 -npernode 1 -x PATH -x LD_LIBRARY_PATH \
      --mca plm_rsh_agent /opt/batch/agent/tools/dstart env -u BASH_ENV bash --noprofile --norc \
      -c 'exec "$@"' bash "${cmd[@]}" > run.log 2>&1
    touch completed.txt
  done
done
