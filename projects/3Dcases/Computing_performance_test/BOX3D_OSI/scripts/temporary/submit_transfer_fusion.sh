#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n transfer_fusion
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=16;mem=49152;gpu=2
#DSUB -N 1
#DSUB -o logs/submit/%J-transfer-fusion.log
#DSUB -e logs/submit/%J-transfer-fusion.log
#DSUB -l cuda122
# 本次融合的临时验证入口；保留为本实验的唯一复现驱动。
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
export FUSION_GPU_COUNT="${FUSION_GPU_COUNT:-2}"
FUSION_PHASE="${FUSION_PHASE:-full}"
FUSION_PERF_REGRID_INT="${FUSION_PERF_REGRID_INT:--1}"
FUSION_PERF_FIXTURE="${FUSION_PERF_FIXTURE:-false}"
CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ROOT="${FUSION_ROOT:-$CASE_DIR/runs/temporary/ab_osi_transfer_fusion}"
REPO_DIR="$(git -C "$CASE_DIR" rev-parse --show-toplevel)"
RUN_DIR="$ROOT/job${CCS_JOB_ID:?}"
mkdir "$RUN_DIR"
awk 'NF >= 2 {print $1 " slots=" $2}' "${CCS_ALLOC_FILE:?}" > "$RUN_DIR/hostfile"
cp "${BASH_SOURCE[0]}" "$RUN_DIR/submit.sh"
cp "$CASE_DIR/scripts/temporary/analyze_transfer_fusion.py" "$RUN_DIR/analyze.py"
hostname > "$RUN_DIR/node.txt"
printf '%s\n' "$FUSION_GPU_COUNT" > "$RUN_DIR/gpu_count.txt"
printf 'regrid_int=%s\nfixture=%s\n' "$FUSION_PERF_REGRID_INT" "$FUSION_PERF_FIXTURE" > "$RUN_DIR/perf_config.txt"
nvidia-smi > "$RUN_DIR/gpu.txt"
module list > "$RUN_DIR/modules.txt" 2>&1
printf 'case\texit_code\n' > "$RUN_DIR/status.tsv"
run() {
    local name=$1 ranks=$2 executable=$3 input=$4
    shift 4
    mkdir "$RUN_DIR/$name"
    cd "$RUN_DIR/$name"
    if [[ "$FUSION_PHASE" == production_check ]]; then
        ln -s "${FUSION_SEED_PREFIX:?}00000064" chk00000064
    fi
    cp "$input" inputs
    printf '%q ' "$executable" inputs "$@" > command.txt
    printf '\n' >> command.txt
    local result=0
    timeout 600 mpirun -hostfile "$RUN_DIR/hostfile" -n "$ranks" -npernode "$ranks" -x PATH -x LD_LIBRARY_PATH -x FUSION_GPU_COUNT \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart /usr/bin/env -u BASH_ENV \
        bash --noprofile --norc -c \
        'export CUDA_VISIBLE_DEVICES=$(( ${OMPI_COMM_WORLD_LOCAL_RANK:-0} % FUSION_GPU_COUNT )); exec "$@"' bash \
        "$executable" inputs "$@" > run.log 2>&1 || result=$?
    printf '%s\t%s\n' "$name" "$result" >> "$RUN_DIR/status.tsv"
    echo "FUSION_CASE $name exit=$result"
}
if [[ "$FUSION_PHASE" == full ]]; then
    for dim in 2 3; do
        run "unit_d${dim}" 1 "$ROOT/candidate/restriction${dim}d.ex" "$ROOT/baseline/config/inputs"
    done
fi
# 进一步融合时可接入同一份生产初始化函数的独立模板/部分父单元检查。
if [[ "$FUSION_PHASE" == full && -x "$ROOT/candidate/sparse_initialization3d.ex" ]]; then
    for ranks in 1 2; do
        run "initialization_r${ranks}" "$ranks" "$ROOT/candidate/sparse_initialization3d.ex" "$ROOT/baseline/config/inputs" amrex.abort_on_unused_inputs=0
    done
    # 负例仍必须在源覆盖缺口处终止，单独保存退出码与诊断。
    run initialization_coverage_negative 1 "$ROOT/candidate/sparse_initialization3d.ex" "$ROOT/baseline/config/inputs" amrex.abort_on_unused_inputs=0 coverage_fail=1
fi
common=(amr.regrid_int=32 amr.plot_int=-1 checkpoint.keep_latest_only=false checkpoint.write_particles=false
    verification.convergence_enabled=false verification.final_state_check=true
    verification.regrid_fixture=true performance.report_int=192 lbm.osi_mpi_pipeline_chunk_bytes=0)
checks=(verification.osi_ab_check=true verification.osi_ab_continue_on_mismatch=false
    verification.osi_step_entry_check_interval=1 verification.osi_check_after_average_valid=true
    verification.check_after_first_fill=true)
if [[ "$FUSION_PHASE" == production_check ]]; then
    mesh=(amr.n_cell=64 64 64 amr.max_level=2 amr.max_grid_size=32 32 32
        amr.blocking_factor_x=16 16 16 amr.blocking_factor_y=16 16 16 amr.blocking_factor_z=16 16 16 amr.n_proper=2)
    for storage in 0 1; do
        for version in baseline candidate; do
            run "prod_d3_i0_s${storage}_${version}" 1 "$ROOT/$version/app3d.ex" "$ROOT/baseline/config/inputs" \
                "${mesh[@]}" checkpoint.begin_step=64 checkpoint.chk_prefix=chk checkpoint.chk_int=128 \
                checkpoint.keep_latest_only=false checkpoint.write_particles=false \
                max_step=128 amr.regrid_int=-1 amr.plot_int=-1 \
                verification.convergence_enabled=false verification.final_state_check=true \
                performance.report_int=64 performance.osi_detail_timing=0 lbm.stream_mode="$storage"
        done
    done
    "$REPO_DIR/.venv/bin/python" "$RUN_DIR/analyze.py" "$RUN_DIR" > "$RUN_DIR/analysis.log"
    echo "FUSION_PRODUCTION_CHECK_DONE $RUN_DIR"
    exit 0
fi
for dim in 2 3; do
    input="$ROOT/baseline/config/inputs"
    mesh=(amr.n_cell=32 32 32 amr.max_level=1 amr.max_grid_size=16 16
        amr.blocking_factor_x=8 8 amr.blocking_factor_y=8 8 amr.blocking_factor_z=8 8 amr.n_proper=2)
    cavity=(0 0 0); periodic=(1 1 1)
    if [[ "$dim" == 2 ]]; then
        input="$input.2d"
        mesh=(amr.n_cell=64 64 amr.max_level=2 amr.max_grid_size=32 32 32
            amr.blocking_factor_x=16 16 16 amr.blocking_factor_y=16 16 16 amr.n_proper=2)
        cavity=(0 0); periodic=(1 1)
    fi
    for mode in 0 1 2; do
        for boundary in cavity periodic; do
            domain=("${cavity[@]}"); seed=false
            if [[ "$boundary" == periodic ]]; then domain=("${periodic[@]}"); seed=true; fi
            for ranks in 1 2; do
                for version in baseline candidate; do
                    # 旧程序的非周期 ghost 诊断也是必要对照。
                    run "stage_d${dim}_i${mode}_r${ranks}_${boundary}_${version}" "$ranks" "$ROOT/$version/app${dim}d.ex" "$input" \
                        "${common[@]}" "${mesh[@]}" "${checks[@]}" max_step=64 checkpoint.chk_int=-1 \
                        lbm.stream_mode=1 lbm.interp_mode="$mode" geometry.is_periodic="${domain[@]}" verification.osi_seed_pattern="$seed"
                done
            done
            for storage in 0 1; do
                name="reg_d${dim}_i${mode}_${boundary}_s${storage}"
                run "${name}_baseline" 2 "$ROOT/baseline/app${dim}d.ex" "$input" \
                    "${common[@]}" "${mesh[@]}" max_step=192 checkpoint.chk_int=192 lbm.stream_mode="$storage" \
                    lbm.interp_mode="$mode" geometry.is_periodic="${domain[@]}" verification.osi_seed_pattern="$seed"
                run "${name}_candidate" 2 "$ROOT/candidate/app${dim}d.ex" "$input" \
                    "${common[@]}" "${mesh[@]}" max_step=192 checkpoint.chk_int=192 lbm.stream_mode="$storage" \
                    lbm.interp_mode="$mode" geometry.is_periodic="${domain[@]}" verification.osi_seed_pattern="$seed" \
                    verification.ddf_reference_checkpoint="$RUN_DIR/${name}_baseline/chk00000192"
            done
        done
    done
done
deep_mesh=(amr.n_cell=64 64 64 amr.max_level=2 amr.max_grid_size=32 32 32
    amr.blocking_factor_x=16 16 16 amr.blocking_factor_y=16 16 16 amr.blocking_factor_z=16 16 16 amr.n_proper=2)
for mode in 0 1 2; do
    for storage in 0 1; do
        name="reg_d3_i${mode}_deep_cavity_s${storage}"
        run "${name}_baseline" 2 "$ROOT/baseline/app3d.ex" "$ROOT/baseline/config/inputs" \
            "${common[@]}" "${deep_mesh[@]}" max_step=192 checkpoint.chk_int=192 \
            lbm.stream_mode="$storage" lbm.interp_mode="$mode"
        run "${name}_candidate" 2 "$ROOT/candidate/app3d.ex" "$ROOT/baseline/config/inputs" \
            "${common[@]}" "${deep_mesh[@]}" max_step=192 checkpoint.chk_int=192 \
            lbm.stream_mode="$storage" lbm.interp_mode="$mode" \
            verification.ddf_reference_checkpoint="$RUN_DIR/${name}_baseline/chk00000192"
    done
done
echo "FUSION_NUMERICS_DONE $RUN_DIR"
"$REPO_DIR/.venv/bin/python" "$RUN_DIR/analyze.py" "$RUN_DIR" > "$RUN_DIR/numerical_report.log"
# 性能只在数值矩阵退出成功后执行；解析器另行检查逐值误差与覆盖。
if awk -F '\t' 'NR>1 && $1!="initialization_coverage_negative" && $2!=0 {bad=1} END {exit bad}' "$RUN_DIR/status.tsv"; then
    perf_mesh=(amr.n_cell=64 64 64 amr.max_level=2 amr.max_grid_size=32 32 32
        amr.blocking_factor_x=16 16 16 amr.blocking_factor_y=16 16 16 amr.blocking_factor_z=16 16 16 amr.n_proper=2)
    run perf_seed 2 "$ROOT/baseline/app3d.ex" "$ROOT/baseline/config/inputs" \
        "${perf_mesh[@]}" max_step=64 amr.regrid_int=32 amr.plot_int=-1 checkpoint.chk_int=64 \
        checkpoint.keep_latest_only=false checkpoint.write_particles=false verification.convergence_enabled=false \
        verification.final_state_check=true lbm.stream_mode=0 performance.report_int=64
    perf_ranks=(1)
    if (( FUSION_GPU_COUNT >= 2 )); then perf_ranks+=(2); fi
    for ranks in "${perf_ranks[@]}"; do
        for storage in 0 1; do
            for repeat in 1 2 3 4; do
                order=(baseline candidate)
                if (( repeat % 2 == 0 )); then order=(candidate baseline); fi
                for version in "${order[@]}"; do
                    run "perf_r${ranks}_s${storage}_${repeat}_${version}" "$ranks" "$ROOT/$version/app3d.ex" "$ROOT/baseline/config/inputs" \
                        "${perf_mesh[@]}" checkpoint.begin_step=64 \
                        checkpoint.chk_prefix="$RUN_DIR/perf_seed/chk" checkpoint.chk_int=-1 \
                        max_step=320 amr.regrid_int="$FUSION_PERF_REGRID_INT" verification.regrid_fixture="$FUSION_PERF_FIXTURE" amr.plot_int=-1 verification.convergence_enabled=false \
                        verification.final_state_check=true performance.report_int=64 performance.osi_detail_timing=0 \
                        lbm.stream_mode="$storage" lbm.interp_mode=0
                done
            done
        done
    done
fi
"$REPO_DIR/.venv/bin/python" "$RUN_DIR/analyze.py" "$RUN_DIR" > "$RUN_DIR/analysis.log"
echo "FUSION_DONE $RUN_DIR"
