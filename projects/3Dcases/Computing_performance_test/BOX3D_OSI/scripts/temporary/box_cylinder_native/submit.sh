#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n box_cylinder_native
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=8;mem=32768;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-box-cylinder-native.log
#DSUB -e logs/submit/%J-box-cylinder-native.log
#DSUB -l cuda122
# 本任务验证及正式运行入口；保留以复现，所有阶段使用同一正式 BOX 冻结程序。
set -euo pipefail
ulimit -c 0
CASE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
REPO="$(git -C "$CASE" rev-parse --show-toplevel)"
TASK="${NATIVE_TASK_DIR:-$CASE/runs/temporary/box_cylinder_native}"
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
source "$CASE/scripts/osi_experiment_common.sh"
NATIVE_HOSTFILE="$TASK/hostfile.${CCS_JOB_ID:-$$}"
osi_write_hostfile "$NATIVE_HOSTFILE"
run(){
 local name=$1;shift
 local dest="$TASK/$name"
 [[ ! -e "$dest" ]] || { echo "拒绝覆盖 $dest";exit 2; }
 mkdir -p "$dest"
 cp "$TASK/inputs" "$dest/inputs"
 cp "$TASK/app.ex" "$dest/app.ex"
 sha256sum "$dest/app.ex" > "$dest/executable.sha256"
 if [[ "$name" == full_* || "$name" == production ]]; then
  ln -s "$TASK/converted/chk00520000" "$dest/chk00520000"
 fi
 if [[ "$name" == small_restart ]]; then ln -s "$TASK/small_osi/chk00000016" "$dest/chk00000016";fi
 if [[ "$name" == full_aligned_restart ]]; then ln -s "$TASK/full_aligned_osi/chk00520032" "$dest/chk00520032";fi
 if [[ "$name" == full_restart ]]; then ln -s "$TASK/full_osi/chk00520016" "$dest/chk00520016";fi
 if [[ "$name" == cavity_* ]]; then sed -i '/^lbm.flow_boundary\|^lbm.reference_velocity\|^lbm.reference_length/d' "$dest/inputs";fi
 cd "$dest"
 osi_record_command command.txt "$dest/app.ex" inputs "$@"
 nvidia-smi > gpu_before.txt
 local rc=0
 mpirun -hostfile "$NATIVE_HOSTFILE" -n 1 -npernode 1 -x PATH -x LD_LIBRARY_PATH \
 --mca plm_rsh_agent /opt/batch/agent/tools/dstart env -u BASH_ENV bash --noprofile --norc \
 -c 'exec "$@"' bash "$dest/app.ex" inputs "$@" > run.log 2>&1 || rc=$?
 printf '%s\n' "$rc" > exit_code.txt
 [[ "$rc" == 0 ]] || return "$rc"
 date -u +%FT%TZ > completed.txt
}
small=(amr.n_cell=32 32 amr.max_level=1 amr.max_grid_size=16 16 amr.blocking_factor_x=8 8 amr.blocking_factor_y=8 8
 ibm.radius=0.125 amr.regrid_int=8 max_step=32 performance.report_int=32 amr.plot_int=32 checkpoint.chk_int=16)
case ${NATIVE_STAGE:-seed} in
 seed) run seed max_step=0 checkpoint.write_initial=true ;;
 matrix)
  run small_ab "${small[@]}" lbm.reference_length=0.25 lbm.stream_mode=0
  run small_osi "${small[@]}" lbm.reference_length=0.25
  run small_restart "${small[@]}" lbm.reference_length=0.25 checkpoint.begin_step=16
  run cavity_implicit "${small[@]}" ibm.enabled=false
  run cavity_explicit "${small[@]}" ibm.enabled=false lbm.flow_boundary=cavity lbm.reference_velocity=0.05 lbm.reference_length=1
  ;;
 full)
  common=(checkpoint.begin_step=520000 max_step=520032 performance.report_int=32 amr.plot_int=32 checkpoint.chk_int=16)
  run full_ab "${common[@]}" lbm.stream_mode=0
  run full_osi "${common[@]}"
  run full_restart "${common[@]}" checkpoint.begin_step=520016
  ;;
 aligned)
  common=(checkpoint.begin_step=520000 max_step=520064 performance.report_int=32 amr.plot_int=32 checkpoint.chk_int=32)
  run full_aligned_ab "${common[@]}" lbm.stream_mode=0
  run full_aligned_osi "${common[@]}"
  run full_aligned_restart "${common[@]}" checkpoint.begin_step=520032
  ;;
 final)
  run seed max_step=0 checkpoint.write_initial=true
  run small_ab "${small[@]}" lbm.reference_length=0.25 lbm.stream_mode=0
  run small_osi "${small[@]}" lbm.reference_length=0.25
  run small_restart "${small[@]}" lbm.reference_length=0.25 checkpoint.begin_step=16
  run cavity_implicit "${small[@]}" ibm.enabled=false
  run cavity_explicit "${small[@]}" ibm.enabled=false lbm.flow_boundary=cavity lbm.reference_velocity=0.05 lbm.reference_length=1
  common=(checkpoint.begin_step=520000 max_step=520032 performance.report_int=32 amr.plot_int=32 checkpoint.chk_int=32)
  run full_ab "${common[@]}" lbm.stream_mode=0
  run full_osi "${common[@]}"
  common=(checkpoint.begin_step=520000 max_step=520064 performance.report_int=32 amr.plot_int=32 checkpoint.chk_int=32)
  run full_aligned_osi "${common[@]}"
  run full_aligned_restart "${common[@]}" checkpoint.begin_step=520032
  ;;
 production)
  TASK="$CASE/runs/box_cylinder_native_520000"
  NATIVE_HOSTFILE="$TASK/hostfile.${CCS_JOB_ID:-$$}"
  osi_write_hostfile "$NATIVE_HOSTFILE"
  "$REPO/.venv/bin/python" "$CASE/scripts/temporary/box_cylinder_native/verify.py" "$TASK" gate
  trap '"$REPO/.venv/bin/python" "$CASE/scripts/temporary/cylinder_btdf1_restart/verify.py" "$TASK" production' EXIT
  run production checkpoint.begin_step=520000
  ;;
 *) echo '未知 NATIVE_STAGE' >&2;exit 2;;
esac
