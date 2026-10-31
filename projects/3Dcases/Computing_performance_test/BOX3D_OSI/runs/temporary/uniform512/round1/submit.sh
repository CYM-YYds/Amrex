#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n uniform512
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=16;mem=49152;gpu=1
#DSUB -N 1
#DSUB -l cuda122
# 本轮临时512³规模测试，复用冻结Q27和guard入口；任务结束后保留复现材料。
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
EXP=/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/uniform512/round1
OUT="$EXP/job${CCS_JOB_ID:?}"
mkdir "$OUT"
cp "$EXP/inputs" "$OUT/inputs"
cp "$0" "$OUT/submit.sh"
awk 'NF>=2 {print $1 " slots=" $2}' "$CCS_ALLOC_FILE" > "$OUT/hostfile"
hostname > "$OUT/node.txt"
nvidia-smi > "$OUT/gpu.txt"
cd "$OUT"
command=(/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/.venv/bin/python /home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/scripts/guard_run.py -- mpirun -hostfile "$OUT/hostfile" -n 1 -npernode 1 -x PATH -x LD_LIBRARY_PATH --mca plm_rsh_agent /opt/batch/agent/tools/dstart /usr/bin/env -u BASH_ENV CUDA_VISIBLE_DEVICES=0 /home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/temporary/uniform512/round1/q27.ex "$OUT/inputs" stop_time=2500000 checkpoint.begin_step=0 amr.n_cell=64 64 64 amr.max_level=0 amr.max_grid_size=16 amr.blocking_factor_x=16 amr.blocking_factor_y=16 amr.blocking_factor_z=16 amr.regrid_int=0 max_step=160 amr.plot_int=-1 checkpoint.chk_int=-1 checkpoint.chk_prefix=chk checkpoint.keep_latest_only=false checkpoint.write_particles=false verification.convergence_enabled=false verification.final_state_check=true verification.osi_seed_pattern=true verification.osi_ab_check=false performance.report_int=80 performance.osi_detail_timing=0 lbm.reynolds_number=1000 lbm.collide_mode=1 lbm.stream_mode=1 amr.n_cell=256 256 256 amr.max_grid_size=256 max_step=1200 performance.report_int=200 geometry.is_periodic=0 0 0 verification.osi_seed_pattern=false verification.final_state_check=false amr.n_cell=512 512 512 amr.max_grid_size=512 verification.final_state_check=true amrex.the_arena_init_size=0)
printf '%q ' "${command[@]}" > command.txt
set +e
"${command[@]}" > guard.log 2>&1
code=$?
printf '%s\n' "$code" > exit_code.txt
exit "$code"
