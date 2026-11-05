#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n osi_cylinder_restart
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=8;mem=32768;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-cylinder-restart.log
#DSUB -e logs/submit/%J-cylinder-restart.log
#DSUB -l cuda122
# 本次续算实验入口；保留以复现实验，不自动用于其他算例。
set -euo pipefail
ulimit -c 0
CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
TASK="${CYLINDER_TASK_DIR:-$CASE_DIR/runs/cylinder_btdf1_restart_520000}"
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
awk 'NF>=2{print $1 " slots=" $2}' "${CCS_ALLOC_FILE:?}" > "$TASK/hostfile"
run_case(){
 local label=$1 mode=$2 last=$3
 label="${label}${CYLINDER_ROUND:+_$CYLINDER_ROUND}"
 local dest="$TASK/$label"
 [[ ! -e "$dest" ]] || { echo "拒绝覆盖 $dest"; exit 1; }
 mkdir -p "$dest"
 cp "$TASK/inputs" "$dest/inputs"
 cp "$TASK/app.ex" "$dest/app.ex"
 sha256sum "$dest/app.ex" > "$dest/executable.sha256"
 ln -s "$TASK/import/chk00520000" "$dest/chk00520000"
 cd "$dest"
 local args=("$dest/app.ex" inputs "lbm.stream_mode=$mode" "max_step=$last")
 if [[ "$mode" == donor ]]; then
  rm "$dest/chk00520000"
  ln -s "$TASK/import_donor/chk00520000" "$dest/chk00520000"
  cp "$TASK/donor.ex" "$dest/app.ex"
  cp "$TASK/donor_inputs" "$dest/inputs"
  args=("$dest/app.ex" inputs "max_step=$last" checkpoint.keep_latest_only=false checkpoint.chk_int=32 amr.plot_int=32)
  sha256sum "$dest/app.ex" > "$dest/executable.sha256"
 fi
 if [[ ${CYLINDER_STAGE:-smoke} == smoke || ${CYLINDER_STAGE:-smoke} == restart ]]; then args+=(amr.plot_int=32 checkpoint.chk_int=32 performance.report_int=32); fi
 if [[ "$label" == native_restart* ]]; then
  ln -s "$TASK/smoke_osi_${CYLINDER_ROUND:-final}/chk00520032" "$dest/chk00520032"
  args+=(checkpoint.begin_step=520032)
 fi
 printf '%q ' "${args[@]}" > command.txt; printf '\n' >> command.txt
 nvidia-smi > gpu_before.txt
 /usr/bin/time -v -o resource_usage.txt mpirun -hostfile "$TASK/hostfile" -n 1 -npernode 1 -x PATH -x LD_LIBRARY_PATH \
 --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
 env -u BASH_ENV bash --noprofile --norc -c 'exec "$@"' bash "${args[@]}" > run.log 2>&1
 nvidia-smi > gpu_after.txt
 date -u +%FT%TZ > completed.txt
}
if [[ ${CYLINDER_STAGE:-smoke} == restart ]]; then
 run_case native_restart 1 520064
elif [[ ${CYLINDER_STAGE:-smoke} == donor ]]; then
 run_case donor donor "$((520000+${CYLINDER_STEPS:-64}))"
elif [[ ${CYLINDER_STAGE:-smoke} == smoke ]]; then
 run_case smoke_ab 0 "$((520000+${CYLINDER_STEPS:-32}))"
 run_case smoke_osi 1 "$((520000+${CYLINDER_STEPS:-32}))"
 run_case donor donor "$((520000+${CYLINDER_STEPS:-32}))"
elif [[ ${CYLINDER_STAGE:-smoke} == production ]]; then
 [[ -f "$TASK/validation_passed.json" ]] || { echo '缺少数值验证通过记录'; exit 1; }
 REPO_DIR="$(git -C "$CASE_DIR" rev-parse --show-toplevel)"
 "$REPO_DIR/.venv/bin/python" - "$TASK" <<'PYGATE'
from pathlib import Path
import json,hashlib,sys
p=Path(sys.argv[1]);report=json.loads((p/'validation_passed.json').read_text())
assert hashlib.sha256((p/'app.ex').read_bytes()).hexdigest()==report['executable_sha256'], '验证程序与待运行程序不一致'
assert max(report['ab_osi_64'].values())<=report['ab_osi_tolerance']
assert hashlib.sha256((p/'inputs').read_bytes()).hexdigest()==report['input_sha256'], '验证输入与待运行输入不一致'
assert report['import_velocity_linf']<=1e-14
PYGATE
 trap '"$REPO_DIR/.venv/bin/python" "$CASE_DIR/scripts/temporary/cylinder_btdf1_restart/verify.py" "$TASK" production' EXIT
 run_case production 1 584000
else
 echo "未知 CYLINDER_STAGE" >&2; exit 2
fi
