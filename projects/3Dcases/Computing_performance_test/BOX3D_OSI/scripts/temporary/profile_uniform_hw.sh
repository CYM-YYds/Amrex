#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n uniform_hw_profile
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=16;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o runs/temporary/uniform_hw_profile/round1/%J-submit.log
#DSUB -e runs/temporary/uniform_hw_profile/round1/%J-submit.log
#DSUB -l cuda122
# 本任务临时复现入口；保留结果期间保留，后续无复现依赖时可删除。
# 复用冻结的256^3 lev0程序和checkpoint，采集端到端基线及GPU时间线/计数器。
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0

CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
REPO_DIR="$(git -C "$CASE_DIR" rev-parse --show-toplevel)"
FROZEN="$CASE_DIR/runs/uniform_256_lev0_t1000"
CHECKPOINT="$CASE_DIR/runs/20261030_102153_job621699/chk00256000"
EXP="$CASE_DIR/runs/temporary/uniform_hw_profile/round1"
OUT="$EXP/job${CCS_JOB_ID:?}"
mkdir "$OUT"
cp "$0" "$OUT/submit.sh"
cp "$FROZEN/application.ex" "$OUT/application.ex"
cp "$FROZEN/inputs" "$OUT/inputs"
cp -R "$FROZEN/src" "$OUT/src"
cp "$FROZEN/git_head.txt" "$OUT/git_head.txt"
sha256sum "$OUT/application.ex" > "$OUT/executable.sha256"
sha256sum "$OUT/inputs" > "$OUT/inputs.sha256"
awk 'NF>=2 {print $1 " slots=" $2}' "${CCS_ALLOC_FILE:?}" > "$OUT/hostfile"
nvidia-smi > "$OUT/gpu.txt"
hostname > "$OUT/node.txt"
module list > "$OUT/modules.txt" 2>&1
NCU=/home/HPCBase/compilers/cuda/12.8.0/bin/ncu
"$NCU" --version > "$OUT/ncu-version.txt" 2>&1 || true

cp "$CASE_DIR/scripts/run_rank.sh" "$OUT/run_rank.sh"
chmod +x "$OUT/run_rank.sh"
printf 'run\texit_code\n' > "$OUT/status.tsv"
common=(inputs checkpoint.begin_step=256000 "checkpoint.chk_prefix=$CHECKPOINT/../chk"
    max_step=256400 stop_time=2500000 amr.n_cell=256 256 256 amr.max_level=0
    amr.max_grid_size=256 amr.regrid_int=0 geometry.is_periodic=0 0 0
    lbm.reynolds_number=1000 lbm.collide_mode=1 lbm.stream_mode=1
    lbm.uniform_nomask_collision=1 performance.uniform_reduce_sync=1
    performance.uniform_grid_path=1 performance.osi_detail_timing=0
    performance.report_int=100 amr.plot_int=-1 checkpoint.chk_int=-1
    verification.convergence_enabled=false verification.final_state_check=false
    verification.osi_ab_check=false verification.osi_seed_pattern=false)
profile_common=(inputs checkpoint.begin_step=256000 "checkpoint.chk_prefix=$CHECKPOINT/../chk"
    max_step=256030 stop_time=2500000 amr.n_cell=256 256 256 amr.max_level=0
    amr.max_grid_size=256 amr.regrid_int=0 geometry.is_periodic=0 0 0
    lbm.reynolds_number=1000 lbm.collide_mode=1 lbm.stream_mode=1
    lbm.uniform_nomask_collision=1 performance.uniform_reduce_sync=1
    performance.uniform_grid_path=1 performance.osi_detail_timing=0
    performance.report_int=100 amr.plot_int=-1 checkpoint.chk_int=-1
    verification.convergence_enabled=false verification.final_state_check=false
    verification.osi_ab_check=false verification.osi_seed_pattern=false)
if [[ ${UNIFORM_HW_NSYS_ONLY:-0} == 1 ]]; then
    NSYS="$(command -v nsys || true)"
    printf '%s\n' "${NSYS:-unavailable}" > "$OUT/nsys-path.txt"
    if [[ -z "$NSYS" ]]; then
        echo 'Nsight Systems executable is unavailable in this module environment.' > "$OUT/nsys-unavailable.txt"
        exit 0
    fi
    mkdir "$OUT/nsys_uniform"
    cd "$OUT/nsys_uniform"
    ln -s "$OUT/inputs" inputs
    nsys_args=(profile --trace=cuda,nvtx,osrt --sample=none --cpuctxsw=none
        --force-overwrite=true --output="$OUT/nsys_uniform/trace")
    printf '%q ' mpirun -hostfile "$OUT/hostfile" -n 1 -npernode 1 -x PATH -x LD_LIBRARY_PATH \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart "$NSYS" "${nsys_args[@]}" \
        "$OUT/application.ex" "${profile_common[@]}" > command.txt
    printf '\n' >> command.txt
    nsys_code=0
    mpirun -hostfile "$OUT/hostfile" -n 1 -npernode 1 -x PATH -x LD_LIBRARY_PATH \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
        bash -lc 'export CUDA_VISIBLE_DEVICES=${OMPI_COMM_WORLD_LOCAL_RANK:-0}; exec "$@"' bash \
        "$NSYS" "${nsys_args[@]}" "$OUT/application.ex" "${profile_common[@]}" \
        > nsys.log 2>&1 || nsys_code=$?
    printf '%s\n' "$nsys_code" > nsys_exit_code.txt
    if [[ "$nsys_code" == 0 && -f trace.nsys-rep ]]; then
        "$NSYS" stats --report cuda_gpu_kern_sum,cuda_gpu_mem_time_sum,cuda_api_sum \
            trace.nsys-rep > stats.txt 2>&1 || true
    fi
    exit 0
fi
run_base() {
    local name=$1
    local code=0
    mkdir "$OUT/$name"
    cd "$OUT/$name"
    ln -s "$OUT/application.ex" main3d.gnu.TPROF.MPI.CUDA.ex
    ln -s "$OUT/run_rank.sh" run_rank.sh
    printf '%q ' "$OUT/application.ex" "$OUT/inputs" "${common[@]:1}" > command.txt
    printf '\n' >> command.txt
    "$REPO_DIR/.venv/bin/python" "$CASE_DIR/scripts/guard_run.py" -- \
        mpirun -hostfile "$OUT/hostfile" -n 1 -npernode 1 -x PATH -x LD_LIBRARY_PATH \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
        bash -lc 'export CUDA_VISIBLE_DEVICES=${OMPI_COMM_WORLD_LOCAL_RANK:-0}; exec "$@"' bash \
        "$OUT/$name/run_rank.sh" "$OUT/inputs" "${common[@]:1}" \
        > run.log 2>&1 || code=$?
    printf '%s\t%s\n' "$name" "$code" >> "$OUT/status.tsv"
    [[ "$code" == 0 ]]
}
for repeat in 1 2 3; do
    run_base "baseline_${repeat}"
done

mkdir "$OUT/ncu_collision"
cd "$OUT/ncu_collision"
ln -s "$OUT/application.ex" main3d.gnu.TPROF.MPI.CUDA.ex
ln -s "$OUT/run_rank.sh" run_rank.sh
ln -s "$OUT/inputs" inputs
ncu_args=(--target-processes all --kernel-name-base demangled --kernel-name regex:Collide
    --launch-skip 16 --launch-count 1 --section SpeedOfLight --section Occupancy
    --section MemoryWorkloadAnalysis --section SchedulerStats --section WarpStateStats
    --section LaunchStats --section InstructionStats --force-overwrite
    --export "$OUT/ncu_collision/collision")
printf '%q ' mpirun -hostfile "$OUT/hostfile" -n 1 -npernode 1 -x PATH -x LD_LIBRARY_PATH \
    --mca plm_rsh_agent /opt/batch/agent/tools/dstart "$NCU" "${ncu_args[@]}" \
    "$OUT/application.ex" "${profile_common[@]}" > command.txt
printf '\n' >> command.txt
profiler_code=0
mpirun -hostfile "$OUT/hostfile" -n 1 -npernode 1 -x PATH -x LD_LIBRARY_PATH \
    --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
    bash -lc 'export CUDA_VISIBLE_DEVICES=${OMPI_COMM_WORLD_LOCAL_RANK:-0}; exec "$@"' bash \
    "$NCU" "${ncu_args[@]}" "$OUT/application.ex" "${profile_common[@]}" \
    > ncu.log 2>&1 || profiler_code=$?
printf '%s\n' "$profiler_code" > ncu_exit_code.txt
if [[ "$profiler_code" == 0 && -f collision.ncu-rep ]]; then
    "$NCU" --import collision.ncu-rep --csv --page details > metrics.csv
    "$NCU" --import collision.ncu-rep --page details > details.txt
fi
printf 'ncu_collision\t%s\n' "$profiler_code" >> "$OUT/status.tsv"
exit 0
