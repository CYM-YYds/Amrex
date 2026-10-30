#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n flat_q27_compare
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=16;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o runs/temporary/flat_q27/round1/%J-submit.log
#DSUB -e runs/temporary/flat_q27/round1/%J-submit.log
#DSUB -l cuda122
# 本轮临时复现入口；保留结果期间保留，不修改正式求解器或师兄原文件。
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
REPO_DIR="$(git -C "$CASE_DIR" rev-parse --show-toplevel)"
EXP="$CASE_DIR/runs/temporary/flat_q27/round1"
OUT="$EXP/job${CCS_JOB_ID:?}"
mkdir "$OUT"
cp "$0" "$OUT/submit.sh"
cp "$EXP/main_q27.cu" "$OUT/main_q27.cu"
cp "$EXP/source_manifest.json" "$OUT/source_manifest.json"
cp "$CASE_DIR/runs/uniform_256_lev0_t1000/application.ex" "$OUT/amrex.ex"
cp "$CASE_DIR/runs/uniform_256_lev0_t1000/inputs" "$OUT/inputs"
cp -R "$CASE_DIR/runs/uniform_256_lev0_t1000/src" "$OUT/amrex_src"
awk 'NF>=2 {print $1 " slots=" $2}' "${CCS_ALLOC_FILE:?}" > "$OUT/hostfile"
nvidia-smi > "$OUT/gpu.txt"
hostname > "$OUT/node.txt"
module list > "$OUT/modules.txt" 2>&1
# MPI头和链接参数来自当前模块，不猜测安装路径。
read -ra mpi_compile <<< "$(mpicxx --showme:compile)"
read -ra mpi_link <<< "$(mpicxx --showme:link)"
# 将MPI包装器的host编译/链接选项转为nvcc可接受的透传参数。
nvcc_mpi=()
for flag in "${mpi_compile[@]}" "${mpi_link[@]}";do
    case "$flag" in
        -pthread) nvcc_mpi+=(-Xcompiler -pthread) ;;
        -Wl,*) nvcc_mpi+=(-Xlinker "${flag#-Wl,}") ;;
        *) nvcc_mpi+=("$flag") ;;
    esac
done
if [[ ${FLAT_VALIDATE_ONLY:-0} == 1 ]];then
    cp "$EXP/verify_source.cu" "$OUT/verify_source.cu"
    cp "$CASE_DIR/scripts/temporary/flat_q27/verify.cu" "$OUT/verify.cu"
    for block in 128 256;do
        nvcc -O3 --use_fast_math -std=c++17 -arch=sm_80 -Xptxas=-v -DnThreads="$block" "${nvcc_mpi[@]}" "$OUT/verify.cu" -o "$OUT/verify${block}.ex" > "$OUT/compile_verify${block}.log" 2>&1
        mpirun -hostfile "$OUT/hostfile" -n 1 -npernode 1 -x PATH -x LD_LIBRARY_PATH --mca plm_rsh_agent /opt/batch/agent/tools/dstart /usr/bin/env -u BASH_ENV CUDA_VISIBLE_DEVICES=0 "$OUT/verify${block}.ex" > "$OUT/verify${block}.log" 2>&1
    done
    printf 'completed\n' > "$OUT/verification_completed.txt"
    exit 0
fi
for block in 128 256; do
    cmd=(nvcc -O3 --use_fast_math -std=c++17 -arch=sm_80 -Xptxas=-v -DnThreads="$block" "${nvcc_mpi[@]}" "$OUT/main_q27.cu" -o "$OUT/flat${block}.ex")
    printf '%q ' "${cmd[@]}" > "$OUT/compile_${block}.command"
    "${cmd[@]}" > "$OUT/compile_${block}.log" 2>&1
 done
sha256sum "$OUT"/*.ex > "$OUT/executables.sha256"
printf 'case\texit_code\n' > "$OUT/status.tsv"
run() {
    local name=$1 executable=$2;shift 2
    mkdir "$OUT/$name"
    cd "$OUT/$name"
    printf '%q ' "$executable" "$@" > command.txt
    printf '\n' >> command.txt
    local code=0
    "$REPO_DIR/.venv/bin/python" "$CASE_DIR/scripts/guard_run.py" -- \
        mpirun -hostfile "$OUT/hostfile" -n 1 -npernode 1 -x PATH -x LD_LIBRARY_PATH \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
        /usr/bin/env -u BASH_ENV CUDA_VISIBLE_DEVICES=0 "$executable" "$@" > guard.log 2>&1 || code=$?
    printf '%s\t%s\n' "$name" "$code" >> "$OUT/status.tsv"
    [[ "$code" == 0 ]]
}
# 先完成非平衡DDF单步验证，再进入性能矩阵。
run verify128 "$OUT/flat128.ex" --verify
run verify256 "$OUT/flat256.ex" --verify
args=("$OUT/inputs" max_step=1200 stop_time=2500000 checkpoint.begin_step=0
    amr.n_cell=256 256 256 amr.max_level=0 amr.max_grid_size=256 amr.regrid_int=0
    amr.plot_int=-1 checkpoint.chk_int=-1 verification.convergence_enabled=false
    verification.final_state_check=true performance.report_int=200 performance.osi_detail_timing=0
    performance.uniform_reduce_sync=1 lbm.uniform_nomask_collision=1 lbm.reynolds_number=1000
    lbm.collide_mode=1 lbm.stream_mode=1 geometry.is_periodic=0 0 0)
for repeat in 1 2 3; do
    order=(amrex flat128 flat256)
    if [[ "$repeat" == 2 ]]; then order=(flat256 flat128 amrex); fi
    for mode in "${order[@]}";do
        if [[ "$mode" == amrex ]];then
            run "perf_${repeat}_${mode}" "$OUT/amrex.ex" "${args[@]}"
        else
            run "perf_${repeat}_${mode}" "$OUT/${mode}.ex"
        fi
    done
done
printf 'completed\n' > "$OUT/completed.txt"
