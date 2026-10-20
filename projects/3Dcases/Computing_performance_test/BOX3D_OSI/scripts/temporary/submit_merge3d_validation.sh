#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n lbm_dimensions
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=16;mem=49152;gpu=2
#DSUB -N 1
#DSUB -o logs/submit/%J-dimensions.log
#DSUB -e logs/submit/%J-dimensions.log
#DSUB -l cuda122
# 合并前三维程序与合并后 A-B/OSI 对照；失败继续记录，最终统一返回。
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TEST_DIR="$CASE_DIR/tests"
REPO_DIR="$(git -C "$CASE_DIR" rev-parse --show-toplevel)"
RUN_DIR="${CASE_DIR}/runs/dimension_merge3d_${CCS_JOB_ID:?}"
mkdir "$RUN_DIR"
cp "${CASE_DIR}/main3d.gnu.TPROF.MPI.CUDA.ex" "$RUN_DIR/app3d.ex"
cp "${DIMENSION_BASELINE_EXE:-${CASE_DIR}/runs/dimension_port_20261015/baseline/app3d.ex}" "$RUN_DIR/baseline3d.ex"
cp "$CASE_DIR/config/inputs" "$RUN_DIR/inputs3d"
cp -a "$CASE_DIR/src" "$CASE_DIR/config" "$RUN_DIR/"
cp "${BASH_SOURCE[0]}" "$RUN_DIR/"
git -C "$REPO_DIR" rev-parse HEAD > "$RUN_DIR/git_head.txt"
git -C "$REPO_DIR" diff > "$RUN_DIR/source.diff"
sha256sum "$RUN_DIR"/*.ex > "$RUN_DIR/executable.sha256"
source "$CASE_DIR/scripts/osi_experiment_common.sh"
osi_write_hostfile "$RUN_DIR/hostfile"
printf 'case\texit_code\n' > "$RUN_DIR/status.tsv"
run() {
    local name=$1 ranks=$2 executable=$3 input=$4
    shift 4
    mkdir "$RUN_DIR/$name"
    cd "$RUN_DIR/$name"
    osi_record_command command.txt "$executable" "$input" "$@"
    local result=0
    osi_launch_mpi hmpi "$RUN_DIR/hostfile" "$ranks" "$ranks" "$executable" "$input" "$@" > run.log 2>&1 || result=$?
    printf '%s\t%s\n' "$name" "$result" >> "$RUN_DIR/status.tsv"
    echo "DIMENSION_CASE $name exit=$result"
    return 0
}
# 本次合并的临时三维生产对照；复用现有验证驱动，随本次证据保留用于复现。
common=(max_step=256 amr.regrid_int=32 amr.plot_int=-1
    checkpoint.chk_int=128 checkpoint.keep_latest_only=false checkpoint.write_particles=false
    verification.convergence_enabled=false verification.final_state_check=true
    performance.report_int=256 lbm.osi_mpi_device_direct=0 lbm.osi_mpi_pipeline_chunk_bytes=0)
mesh3=(amr.n_cell=64 64 64 amr.max_level=1 amr.max_grid_size=32 32
    amr.blocking_factor_x=16 16 amr.blocking_factor_y=16 16 amr.blocking_factor_z=16 16)
for mode in 0 1 2; do
    run "d3_baseline_i${mode}" 2 "$RUN_DIR/baseline3d.ex" "$RUN_DIR/inputs3d" \
        "${common[@]}" "${mesh3[@]}" max_step=64 checkpoint.chk_int=64 lbm.stream_mode=1 lbm.interp_mode="$mode"
    for storage in 0 1; do
        run "d3_current_i${mode}_s${storage}" 2 "$RUN_DIR/app3d.ex" "$RUN_DIR/inputs3d" \
            "${common[@]}" "${mesh3[@]}" max_step=64 checkpoint.chk_int=64 lbm.stream_mode="$storage" lbm.interp_mode="$mode" \
            verification.ddf_reference_checkpoint="$RUN_DIR/d3_baseline_i${mode}/chk00000064"
    done
done
# 旧格式三维 checkpoint 无模型元数据仍可重启；二维必须在解析 BoxArray 前拒绝它。
run d3_legacy_restart 2 "$RUN_DIR/app3d.ex" "$RUN_DIR/inputs3d" "${common[@]}" "${mesh3[@]}" \
    max_step=64 checkpoint.begin_step=64 checkpoint.chk_prefix="$RUN_DIR/d3_baseline_i0/chk" \
    verification.ddf_reference_checkpoint="$RUN_DIR/d3_baseline_i0/chk00000064"
awk -F "\t" 'NR > 1 && $2 != 0 { bad=1 } END { exit bad }' "$RUN_DIR/status.tsv"
