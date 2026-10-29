#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n uniform_dispatch
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=16;mem=49152;gpu=2
#DSUB -N 1
#DSUB -o runs/temporary/uniform_dispatch/%J-submit.log
#DSUB -e runs/temporary/uniform_dispatch/%J-submit.log
#DSUB -l cuda122
# 本次单层调度的临时验证入口；保留结果期间作为复现材料，不独立长期维护。
# 旧入口绑定历史 AMR/碰撞实验，故本次只复用 MPI、命令记录和退出保护机制。
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
REPO_DIR="$(git -C "$CASE_DIR" rev-parse --show-toplevel)"
EXP="${UNIFORM_EXPERIMENT_DIR:-$CASE_DIR/runs/temporary/uniform_dispatch/round1}"
if [[ ${UNIFORM_KERNEL_EXPERIMENT:-0} == 1 ]]; then
    EXP="$CASE_DIR/runs/temporary/uniform_kernels/round1"
fi
OUT="$EXP/job${CCS_JOB_ID:?}"
mkdir "$OUT"
cp "$0" "$OUT/submit.sh"
cp "$CASE_DIR/scripts/temporary/analyze_uniform_dispatch.py" "$OUT/analyze.py"
if [[ ${UNIFORM_KERNEL_EXPERIMENT:-0} == 1 ]]; then
    cp "$CASE_DIR/scripts/temporary/analyze_uniform_dispatch.py" "$OUT/analyze_uniform_dispatch.py"
    cp "$CASE_DIR/scripts/temporary/analyze_uniform_kernels.py" "$OUT/analyze.py"
fi
source "$CASE_DIR/scripts/osi_experiment_common.sh"
export -f osi_launch_mpi
osi_write_hostfile "$OUT/hostfile"
nvidia-smi > "$OUT/gpu.txt"
hostname > "$OUT/node.txt"
module list > "$OUT/modules.txt" 2>&1
printf 'case\tranks\texit_code\n' > "$OUT/status.tsv"
common=(max_step=64 stop_time=2500000
    amr.n_cell="${UNIFORM_CHECK_CELLS:-32}" "${UNIFORM_CHECK_CELLS:-32}" "${UNIFORM_CHECK_CELLS:-32}" amr.max_level=0
    amr.max_grid_size=16 amr.blocking_factor_x=8 amr.blocking_factor_y=8 amr.blocking_factor_z=8
    amr.regrid_int=16 amr.plot_int=-1 checkpoint.chk_int=-1 checkpoint.begin_step=0
    checkpoint.chk_prefix=chk checkpoint.keep_latest_only=false checkpoint.write_particles=false
    lbm.collide_mode=1 lbm.stream_mode=1 lbm.osi_mpi_device_direct=0
    performance.osi_detail_timing=0 performance.report_int=32
    verification.convergence_enabled=false verification.final_state_check=true
    verification.osi_seed_pattern=true)
run() {
    local name=$1 ranks=$2 executable=$3
    shift 3
    mkdir "$OUT/$name"
    cd "$OUT/$name"
    ln -s "$executable" main3d.gnu.TPROF.MPI.CUDA.ex
    cp "$CASE_DIR/scripts/run_rank.sh" run_rank.sh
    chmod +x run_rank.sh
    osi_record_command command.txt "$executable" "$EXP/inputs" "${common[@]}" "$@"
    local code=0
    "$REPO_DIR/.venv/bin/python" "$CASE_DIR/scripts/guard_run.py" -- \
        bash --noprofile --norc -c 'osi_launch_mpi "$@"' bash hmpi "$OUT/hostfile" "$ranks" "$ranks" \
        "$OUT/$name/run_rank.sh" "$EXP/inputs" "${common[@]}" "$@" || code=$?
    printf '%s\t%s\t%s\n' "$name" "$ranks" "$code" >> "$OUT/status.tsv"
    echo "UNIFORM_CASE $name exit=$code"
    [[ "$code" == 0 ]]
}
if [[ ${UNIFORM_KERNEL_EXPERIMENT:-0} == 1 ]]; then
    common+=(amr.n_cell=64 64 64 amr.regrid_int=0)
    for ranks in 1 2; do
        for storage in 0 1; do
            for boundary in cavity periodic; do
                domain=(0 0 0)
                [[ "$boundary" == periodic ]] && domain=(1 1 1)
                name="check_r${ranks}_s${storage}_${boundary}"
                for variant in baseline sync nomask; do
                    extra=(checkpoint.chk_int=32)
                    if [[ "$variant" != baseline ]]; then
                        extra=(verification.ddf_reference_checkpoint="$OUT/${name}_baseline/chk00000064")
                    fi
                    run "${name}_${variant}" "$ranks" "$EXP/$variant.ex" lbm.stream_mode="$storage" \
                        geometry.is_periodic="${domain[@]}" "${extra[@]}"
                done
            done
        done
    done
    for variant in sync nomask; do
        run "hooks_$variant" 2 "$EXP/$variant.ex" max_step=16 verification.osi_ab_check=true \
            verification.osi_ab_continue_on_mismatch=false verification.osi_step_entry_check_interval=1 \
            geometry.is_periodic=1 1 1
        run "restart_$variant" 1 "$EXP/$variant.ex" checkpoint.begin_step=32 \
            checkpoint.chk_prefix="$OUT/check_r1_s1_cavity_baseline/chk" \
            verification.ddf_reference_checkpoint="$OUT/check_r1_s1_cavity_baseline/chk00000064"
    done
    "$REPO_DIR/.venv/bin/python" "$OUT/analyze.py" "$OUT" --checks-only
    # 三种程序分别计时，不运行叠加候选；六种顺序各一次，均用同节点共享基线配对。
    orders=('baseline sync nomask' 'nomask sync baseline' 'sync baseline nomask'
            'nomask baseline sync' 'sync nomask baseline' 'baseline nomask sync')
    for spec in '128 128 1' '128 32 1' '256 128 1' '128 32 2'; do
        read -r cells box ranks <<< "$spec"
        for block in 1 2 3 4 5 6; do
            read -ra variants <<< "${orders[$((block-1))]}"
            for variant in "${variants[@]}"; do
                run "perf_n${cells}_b${box}_r${ranks}_p${block}_${variant}" "$ranks" "$EXP/$variant.ex" \
                    max_step=1200 amr.n_cell="$cells" "$cells" "$cells" amr.max_grid_size="$box" \
                    performance.report_int=200 verification.final_state_check=false
            done
        done
    done
    "$REPO_DIR/.venv/bin/python" "$OUT/analyze.py" "$OUT"
    exit 0
fi
if [[ -n ${UNIFORM_CHECKS_FROM:-} ]]; then
    # 只复用已通过的独立数值组；失败AMR组重跑，计时一律在本作业内重新执行。
    previous="$EXP/job${UNIFORM_CHECKS_FROM}"
    printf '%s\n' "$previous" > "$OUT/reused_checks.txt"
    while IFS=$'\t' read -r name ranks code; do
        [[ "$name" == case || "$name" == *_amr || "$name" == perf_* ]] && continue
        [[ "$code" == 0 ]]
        ln -s "$previous/$name" "$OUT/$name"
        printf '%s\t%s\t%s\n' "$name" "$ranks" "$code" >> "$OUT/status.tsv"
    done < "$previous/status.tsv"
else
for ranks in 1 2; do
    for storage in 0 1; do
        for boundary in cavity periodic; do
            domain=(0 0 0)
            [[ "$boundary" == periodic ]] && domain=(1 1 1)
            name="r${ranks}_s${storage}_${boundary}"
            run "base_$name" "$ranks" "$EXP/baseline.ex" lbm.stream_mode="$storage" \
                geometry.is_periodic="${domain[@]}" checkpoint.chk_int=64
            run "new_$name" "$ranks" "$EXP/candidate.ex" lbm.stream_mode="$storage" \
                geometry.is_periodic="${domain[@]}" \
                verification.ddf_reference_checkpoint="$OUT/base_$name/chk00000064"
        done
    done
done
# 保存中间点后恢复，检查相位归零后的单层推进和输出生命周期。
run restart_seed 1 "$EXP/candidate.ex" checkpoint.chk_int=32 amr.plot_int=32
# 源检查点前缀只用于读，chk_int=-1 禁止向源目录写出。
run restart_check 1 "$EXP/candidate.ex" checkpoint.begin_step=32 \
    checkpoint.chk_prefix="$OUT/restart_seed/chk" \
    verification.ddf_reference_checkpoint="$OUT/restart_seed/chk00000064"
run hooks 2 "$EXP/candidate.ex" max_step=16 verification.osi_ab_check=true \
    verification.osi_ab_continue_on_mismatch=false verification.osi_step_entry_check_interval=1 \
    geometry.is_periodic=1 1 1
fi
# max_level>0 即使初始 finest_level=0 也必须保留 AMR；第32步创建细层。
for variant in base new; do
    executable="$EXP/baseline.ex"
    extra=(checkpoint.chk_int=40)
    if [[ "$variant" == new ]]; then
        executable="$EXP/candidate.ex"
        extra=(verification.ddf_reference_checkpoint="$OUT/base_amr/chk00000040")
    fi
    run "${variant}_amr" 1 "$executable" max_step=40 amr.max_level=1 amr.plot_int=40 \
        amr.max_grid_size=32 32 amr.blocking_factor_x=8 8 \
        amr.blocking_factor_y=8 8 amr.blocking_factor_z=8 8 amr.regrid_int=8 \
        verification.regrid_fixture=true "${extra[@]}"
done
"$REPO_DIR/.venv/bin/python" "$OUT/analyze.py" "$OUT" --checks-only
[[ ${UNIFORM_CHECKS_ONLY:-0} == 1 ]] && exit 0
# 同一程序、同一节点、交替AB/BA；另设 regrid=0 隔离伪重网格的影响。
for spec in '64 32 1 32' '128 128 1 32' '128 32 1 32' '128 64 2 32' '128 128 1 0'; do
    read -r cells box ranks regrid <<< "$spec"
    for pair in 1 2 3 4 5; do
        order=(0 1)
        (( pair % 2 == 0 )) && order=(1 0)
        for enabled in "${order[@]}"; do
            run "perf_n${cells}_b${box}_r${ranks}_g${regrid}_p${pair}_v${enabled}" "$ranks" "$EXP/candidate.ex" \
                max_step=1200 amr.n_cell="$cells" "$cells" "$cells" amr.max_grid_size="$box" \
                amr.regrid_int="$regrid" performance.report_int=200 \
                performance.uniform_grid_path="$enabled" verification.final_state_check=false
        done
    done
done
"$REPO_DIR/.venv/bin/python" "$OUT/analyze.py" "$OUT"
