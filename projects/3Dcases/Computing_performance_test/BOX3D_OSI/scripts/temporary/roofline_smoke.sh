#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n roofline_smoke
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=16;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o runs/temporary/roofline_smoke/round1/%J-submit.log
#DSUB -e runs/temporary/roofline_smoke/round1/%J-submit.log
#DSUB -l cuda122
# AI 临时测试：验证当前程序的 Roofline 采集权限；保留复现材料期间保留，可在无依赖后删除。
# 旧剖析入口绑定历史程序和实验，本入口使用本轮已构建并冻结的程序，不回写历史材料。
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EXP="$CASE_DIR/runs/temporary/roofline_smoke/round1"
OUT="$EXP/job${CCS_JOB_ID:?}"
mkdir "$OUT"
cp "$0" "$OUT/submit.sh"
awk 'NF>=2 {print $1 " slots=" $2}' "${CCS_ALLOC_FILE:?}" > "$OUT/hostfile"
hostname > "$OUT/node.txt"
nvidia-smi > "$OUT/gpu.txt"
nvidia-smi -q > "$OUT/gpu-detail.txt"
cat /proc/driver/nvidia/params > "$OUT/driver-params.txt" || true
module list > "$OUT/modules.txt" 2>&1
NCU=/home/HPCBase/compilers/cuda/12.8.0/bin/ncu
"$NCU" --version > "$OUT/ncu-version.txt"
"$NCU" --list-sections > "$OUT/ncu-sections.txt"
launcher=(mpirun -hostfile "$OUT/hostfile" -n 1 -npernode 1 -x PATH -x LD_LIBRARY_PATH
    --mca plm_rsh_agent /opt/batch/agent/tools/dstart
    /usr/bin/env -u BASH_ENV CUDA_VISIBLE_DEVICES=0)
args=("$EXP/inputs" checkpoint.begin_step=256000
    "checkpoint.chk_prefix=$CASE_DIR/runs/20261030_102153_job621699/chk"
    max_step=256100 stop_time=2500000 amr.n_cell=256 256 256 amr.max_level=0
    amr.max_grid_size=256 amr.regrid_int=0 geometry.is_periodic=0 0 0
    lbm.reynolds_number=1000 lbm.collide_mode=1 lbm.stream_mode=1
    lbm.uniform_nomask_collision=1 performance.uniform_reduce_sync=1
    performance.osi_detail_timing=0 performance.report_int=20
    amr.plot_int=-1 checkpoint.chk_int=-1 checkpoint.keep_latest_only=false
    verification.convergence_enabled=false verification.final_state_check=true
    verification.osi_ab_check=false verification.osi_seed_pattern=false)
printf 'run\texit_code\n' > "$OUT/status.tsv"
for repeat in 1 2 3; do
    mkdir "$OUT/baseline_$repeat"
    cd "$OUT/baseline_$repeat"
    # 使用本地链接读取重启；若写最终输出，前缀留在本次运行内。
    ln -s "$CASE_DIR/runs/20261030_102153_job621699/chk00256000" chk00256000
    output_args=(checkpoint.chk_prefix=chk)
    if [[ "$repeat" == 3 ]]; then
        output_args+=(checkpoint.chk_int=100 amr.plot_int=100 amr.plot_file=plt_)
    fi
    printf '%q ' "${launcher[@]}" "$EXP/application.ex" "${args[@]}" "${output_args[@]}" > command.txt
    printf '\n' >> command.txt
    code=0
    timeout 180 "${launcher[@]}" "$EXP/application.ex" "${args[@]}" "${output_args[@]}" > run.log 2>&1 || code=$?
    printf 'baseline_%s\t%s\n' "$repeat" "$code" >> "$OUT/status.tsv"
    [[ "$code" == 0 ]] || exit "$code"
done
mkdir "$OUT/ncu_collision"
cd "$OUT/ncu_collision"
profile=(--target-processes all --kernel-name-base demangled --kernel-name regex:Collide
    --launch-skip 16 --launch-count 1 --section SpeedOfLight
    --section SpeedOfLight_RooflineChart --section MemoryWorkloadAnalysis
    --section LaunchStats --section Occupancy --force-overwrite --export "$PWD/collision")
printf '%q ' "${launcher[@]}" "$NCU" "${profile[@]}" "$EXP/application.ex" "${args[@]}" max_step=256030 verification.final_state_check=false > command.txt
printf '\n' >> command.txt
code=0
timeout 180 "${launcher[@]}" "$NCU" "${profile[@]}" "$EXP/application.ex" "${args[@]}" \
    max_step=256030 verification.final_state_check=false > ncu.log 2>&1 || code=$?
printf 'ncu_collision\t%s\n' "$code" >> "$OUT/status.tsv"
if [[ "$code" == 0 && -f collision.ncu-rep ]]; then
    "$NCU" --import collision.ncu-rep --csv --page details --print-details all > metrics.csv
    "$NCU" --import collision.ncu-rep --page details --print-details all > details.txt
fi
# 批处理成功不等于采集成功，退出状态以 status.tsv 为准。
exit "$code"
