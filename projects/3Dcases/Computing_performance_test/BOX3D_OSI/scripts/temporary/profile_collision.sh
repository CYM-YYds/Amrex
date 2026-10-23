#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n collide_profile
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-profile.log
#DSUB -e logs/submit/%J-profile.log
#DSUB -l cuda122
# 本次真实 L2 碰撞分析的复现入口，随实验材料保留。
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$CASE_DIR/runs/collision_profile_20261023/job${CCS_JOB_ID:?}"
mkdir -p "$OUT"
cp "$CASE_DIR/runs/collision_batch_20261023/job617902/app.ex" "$OUT/app.ex"
cp "$CASE_DIR/runs/collision_batch_20261023/job617902/inputs" "$OUT/inputs"
cp "$0" "$OUT/submit.sh"
sha256sum "$OUT/app.ex" > "$OUT/executable.sha256"
awk 'NF>=2 {print $1 " slots=" $2}' "${CCS_ALLOC_FILE:?}" > "$OUT/hostfile"
nvidia-smi > "$OUT/gpu.txt"
NCU=/home/HPCBase/compilers/cuda/12.8.0/bin/ncu
"$NCU" --version > "$OUT/ncu-version.txt"
cd "$OUT"
args=(inputs amr.plot_int=-1 checkpoint.chk_int=-1 verification.convergence_enabled=false
 performance.osi_detail_timing=0 lbm.stream_mode=1 lbm.collide_mode=1 lbm.collision_batch_mode=0
 checkpoint.begin_step=256000 "checkpoint.chk_prefix=$CASE_DIR/runs/20261019_124153_job615364/chk00256000/../chk"
 max_step=256012 amr.max_level=2 amr.regrid_int=-1 performance.report_int=4
 amr.blocking_factor_x=32 32 32 amr.blocking_factor_y=32 32 32 amr.blocking_factor_z=32 32 32)
# 每 coarse step 的碰撞发射数为1+2*10+4*33=153，8步预热后跳过L0和首个L1的11次发射。
profile=(--target-processes all --kernel-name-base demangled --kernel-name regex:Collide
 --launch-skip 1235 --launch-count 33 --section SpeedOfLight --section Occupancy
 --section MemoryWorkloadAnalysis --section SchedulerStats --section WarpStateStats
 --section LaunchStats --section InstructionStats --force-overwrite --export "$OUT/collision")
printf '%q ' "$NCU" "${profile[@]}" ./app.ex "${args[@]}" > command.txt
code=0
mpirun -hostfile "$OUT/hostfile" -n 1 -npernode 1 -x PATH -x LD_LIBRARY_PATH \
 --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
 /usr/bin/env -u BASH_ENV CUDA_VISIBLE_DEVICES=0 "$NCU" "${profile[@]}" ./app.ex "${args[@]}" > run.log 2>&1 || code=$?
printf '%s\n' "$code" > exit_code.txt
if [[ "$code" == 0 && -f collision.ncu-rep ]]; then
 "$NCU" --import collision.ncu-rep --csv --page details > metrics.csv
 "$NCU" --import collision.ncu-rep --page details > details.txt
fi
exit "$code"
