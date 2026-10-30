#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n collision_factors
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=16;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o runs/temporary/collision_factors/round1/%J-submit.log
#DSUB -e runs/temporary/collision_factors/round1/%J-submit.log
#DSUB -l cuda122
# 本轮临时实验入口；只构建隔离副本，保留结果期间作为复现依赖。
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
REPO_DIR="$(git -C "$CASE_DIR" rev-parse --show-toplevel)"
EXP="$CASE_DIR/runs/temporary/collision_factors/round1"
OUT="$EXP/job${CCS_JOB_ID:?}"
mkdir "$OUT"
cp "$0" "$OUT/submit.sh"
cp "$CASE_DIR/scripts/temporary/collision_factors/analyze.py" "$OUT/analyze.py"
cp "$EXP/inputs" "$OUT/inputs"
cp "$EXP/manifest.json" "$OUT/manifest.json"
cp "$EXP/candidate.patch" "$OUT/candidate.patch"
cp -R "$EXP/build/src" "$OUT/src"
cp -R "$EXP/build/config" "$OUT/config"
(cd "$EXP/build";make -j8 DIM=3 PRECISION=FLOAT) > "$OUT/compile.log" 2>&1
cp "$EXP/build/main3d.gnu.FLOAT.TPROF.MPI.CUDA.ex" "$OUT/candidate.ex"
cp "$EXP/frozen_baseline.ex" "$OUT/frozen.ex"
sha256sum "$OUT"/*.ex > "$OUT/executables.sha256"
awk 'NF>=2 {print $1 " slots=" $2}' "${CCS_ALLOC_FILE:?}" > "$OUT/hostfile"
nvidia-smi > "$OUT/gpu.txt"
hostname > "$OUT/node.txt"
module list > "$OUT/modules.txt" 2>&1
printf 'case\tmode\texit_code\n' > "$OUT/status.tsv"
common=("$OUT/inputs" max_step=160 stop_time=2500000 checkpoint.begin_step=0
    amr.n_cell=64 64 64 amr.max_level=0 amr.max_grid_size=64 amr.regrid_int=0
    amr.plot_int=-1 checkpoint.chk_int=-1 checkpoint.chk_prefix=chk checkpoint.keep_latest_only=false
    checkpoint.write_particles=false verification.convergence_enabled=false
    verification.final_state_check=true verification.osi_seed_pattern=true verification.osi_ab_check=false
    performance.report_int=80 performance.osi_detail_timing=0 performance.uniform_grid_path=1
    performance.uniform_reduce_sync=1 lbm.uniform_nomask_collision=1 lbm.reynolds_number=1000
    lbm.collide_mode=1 lbm.stream_mode=1 geometry.is_periodic=0 0 0)
run() {
    local name=$1 mode=$2;shift 2
    local executable="$OUT/candidate.ex"
    [[ "$mode" == frozen ]] && executable="$OUT/frozen.ex"
    mkdir "$OUT/$name";cd "$OUT/$name"
    printf '%q ' env BOX3D_COLLISION_TEST_MODE="$mode" "$executable" "${common[@]}" "$@" > command.txt
    printf '\n' >> command.txt
    local code=0
    "$REPO_DIR/.venv/bin/python" "$CASE_DIR/scripts/guard_run.py" -- \
        mpirun -hostfile "$OUT/hostfile" -n 1 -npernode 1 -x PATH -x LD_LIBRARY_PATH \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart /usr/bin/env -u BASH_ENV \
        CUDA_VISIBLE_DEVICES=0 BOX3D_COLLISION_TEST_MODE="$mode" "$executable" "${common[@]}" "$@" > guard.log 2>&1 || code=$?
    printf '%s\t%s\t%s\n' "$name" "$mode" "$code" >> "$OUT/status.tsv"
    [[ "$code" == 0 ]]
}
# 独立冻结基线，全DDF逐值比较，含相位跨环、多Fab与周期/非周期。
for spec in '64 64 0 160' '64 16 0 160' '64 16 1 160' '256 256 0 320';do
    read -r cells box periodic steps <<< "$spec"
    name="n${cells}_b${box}_p${periodic}"
    chkint=$steps
    [[ "$cells" == 64 && "$box" == 64 ]] && chkint=80
    run "check_${name}_frozen" frozen amr.n_cell="$cells" "$cells" "$cells" amr.max_grid_size="$box" geometry.is_periodic="$periodic" "$periodic" "$periodic" max_step="$steps" checkpoint.chk_int="$chkint"
    reference="$OUT/check_${name}_frozen/chk$(printf '%08d' "$steps")"
    for mode in 0 1 2 3;do
        run "check_${name}_m${mode}" "$mode" amr.n_cell="$cells" "$cells" "$cells" amr.max_grid_size="$box" geometry.is_periodic="$periodic" "$periodic" "$periodic" max_step="$steps" verification.ddf_reference_checkpoint="$reference"
    done
done
for mode in 0 1 2 3;do
    run "restart_m${mode}" "$mode" checkpoint.begin_step=80 checkpoint.chk_prefix="$OUT/check_n64_b64_p0_frozen/chk" verification.ddf_reference_checkpoint="$OUT/check_n64_b64_p0_frozen/chk00000160"
done
"$REPO_DIR/.venv/bin/python" "$OUT/analyze.py" "$OUT" --checks-only
# 五种模式在同一分配内循环换序，每次剔除首200步；冻结程序控制重编/host分派影响。
orders=('frozen 0 1 2 3' '3 2 1 0 frozen' '1 2 3 frozen 0' '0 frozen 3 2 1' '2 3 frozen 0 1')
for repeat in 1 2 3 4 5;do
    read -ra order <<< "${orders[$((repeat-1))]}"
    for mode in "${order[@]}";do
        run "perf_${repeat}_m${mode}" "$mode" amr.n_cell=256 256 256 amr.max_grid_size=256 max_step=1200 performance.report_int=200 verification.osi_seed_pattern=false verification.final_state_check=false
    done
done
"$REPO_DIR/.venv/bin/python" "$OUT/analyze.py" "$OUT"
printf 'completed\n' > "$OUT/completed.txt"
