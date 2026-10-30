#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n collision_amr
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=16;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o runs/temporary/collision_amr/round1/%J-submit.log
#DSUB -e runs/temporary/collision_amr/round1/%J-submit.log
#DSUB -l cuda122
# 本轮临时验证入口；单层正式接入与AMR候选分开冻结，不修改用户inputs或历史checkpoint。
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
REPO_DIR="$(git -C "$CASE_DIR" rev-parse --show-toplevel)"
if [[ "${COORD_STAGE_TEST:-0}" == seam ]]; then
    exec "$REPO_DIR/.venv/bin/python" "$CASE_DIR/scripts/temporary/collision_amr/stages.py" seam
elif [[ "${COORD_STAGE_TEST:-0}" == 1 ]]; then
    exec "$REPO_DIR/.venv/bin/python" "$CASE_DIR/scripts/temporary/collision_amr/stages.py" run
fi
EXP="$CASE_DIR/runs/temporary/collision_amr/round1"
OUT="$EXP/job${CCS_JOB_ID:?}"
mkdir "$OUT";cp "$0" "$OUT/submit.sh"
cp "$CASE_DIR/scripts/temporary/collision_amr/analyze.py" "$OUT/analyze.py"
cp "$EXP/inputs" "$OUT/inputs"
cp -R "$EXP/build/src" "$OUT/production_src"
cp -R "$EXP/build/config" "$OUT/config"
(cd "$EXP/build";make -j8 DIM=3 PRECISION=FLOAT) > "$OUT/production_compile.log" 2>&1
cp "$EXP/build/main3d.gnu.FLOAT.TPROF.MPI.CUDA.ex" "$OUT/production.ex"
cp "$EXP/amr_candidate_advance.cpp" "$EXP/build/src/AmrCoreLBM_advance.cpp"
(cd "$EXP/build";make -j8 DIM=3 PRECISION=FLOAT) > "$OUT/candidate_compile.log" 2>&1
cp "$EXP/build/main3d.gnu.FLOAT.TPROF.MPI.CUDA.ex" "$OUT/candidate.ex"
cp -R "$EXP/build/src" "$OUT/candidate_src"
cp "$EXP/frozen.ex" "$OUT/frozen.ex"
sha256sum "$OUT"/*.ex > "$OUT/executables.sha256"
awk 'NF>=2 {print $1 " slots=" $2}' "${CCS_ALLOC_FILE:?}" > "$OUT/hostfile"
nvidia-smi > "$OUT/gpu.txt";hostname > "$OUT/node.txt"
module list > "$OUT/modules.txt" 2>&1
printf 'case\tmode\texit_code\n' > "$OUT/status.tsv"
common=("$OUT/inputs" max_step=160 stop_time=2500000 checkpoint.begin_step=0
    amr.n_cell=64 64 64 amr.max_level=0 amr.max_grid_size=64 amr.regrid_int=0
    amr.blocking_factor_x=16 16 16 amr.blocking_factor_y=16 16 16 amr.blocking_factor_z=16 16 16
    amr.plot_int=-1 checkpoint.chk_int=-1 checkpoint.chk_prefix=chk checkpoint.keep_latest_only=false
    checkpoint.write_particles=false verification.convergence_enabled=false
    verification.final_state_check=true verification.osi_seed_pattern=true verification.osi_ab_check=false
    performance.report_int=80 performance.osi_detail_timing=0 performance.uniform_grid_path=1
    performance.uniform_reduce_sync=1 lbm.uniform_nomask_collision=1 lbm.reynolds_number=1000
    lbm.collide_mode=1 lbm.stream_mode=1 geometry.is_periodic=0 0 0)
run() {
    local name=$1 mode=$2;shift 2
    local executable="$OUT/candidate.ex"
    [[ "$mode" == production ]] && executable="$OUT/production.ex"
    [[ "$mode" == frozen ]] && executable="$OUT/frozen.ex"
    mkdir "$OUT/$name";cd "$OUT/$name"
    printf '%q ' env BOX3D_AMR_COLLISION_TEST_MODE="$mode" "$executable" "${common[@]}" "$@" > command.txt;printf '\n' >> command.txt
    local code=0
    "$REPO_DIR/.venv/bin/python" "$CASE_DIR/scripts/guard_run.py" -- mpirun -hostfile "$OUT/hostfile" -n 1 -npernode 1 -x PATH -x LD_LIBRARY_PATH --mca plm_rsh_agent /opt/batch/agent/tools/dstart /usr/bin/env -u BASH_ENV CUDA_VISIBLE_DEVICES=0 BOX3D_AMR_COLLISION_TEST_MODE="$mode" "$executable" "${common[@]}" "$@" > guard.log 2>&1 || code=$?
    printf '%s\t%s\t%s\n' "$name" "$mode" "$code" >> "$OUT/status.tsv"
    [[ "$code" == 0 ]]
}
# 正式单层接入对照前轮独立冻结程序产生的检查点，包含跨环与restart。
PREVIOUS="$CASE_DIR/runs/temporary/collision_factors/round1/job621730"
for spec in '64 64 0 160' '64 16 0 160' '64 16 1 160' '256 256 0 320';do
    read -r cells box periodic steps <<< "$spec"
    name="n${cells}_b${box}_p${periodic}"
    run "uniform_${name}" production amr.n_cell="$cells" "$cells" "$cells" amr.max_grid_size="$box" geometry.is_periodic="$periodic" "$periodic" "$periodic" max_step="$steps" verification.ddf_reference_checkpoint="$PREVIOUS/check_${name}_frozen/chk$(printf '%08d' "$steps")"
done
run uniform_restart production checkpoint.begin_step=80 checkpoint.chk_prefix="$PREVIOUS/check_n64_b64_p0_frozen/chk" verification.ddf_reference_checkpoint="$PREVIOUS/check_n64_b64_p0_frozen/chk00000160"
run uniform_lockstep production max_step=16 verification.osi_ab_check=true verification.osi_ab_continue_on_mismatch=false verification.osi_step_entry_check_interval=1 geometry.is_periodic=1 1 1
# 动态三级小网格；最终输出先完成平均，再写checkpoint，保证covered粗层比较阶段一致。
for periodic in 0 1;do
    mesh=(amr.n_cell=32 32 32 amr.max_level=2 amr.max_grid_size=16 16 16
        amr.blocking_factor_x=8 8 8 amr.blocking_factor_y=8 8 8 amr.blocking_factor_z=8 8 8
        amr.regrid_int=8 max_step=80 performance.report_int=40 verification.regrid_fixture=true
        amr.plot_int=80 geometry.is_periodic="$periodic" "$periodic" "$periodic")
    run "dynamic_p${periodic}_frozen" frozen "${mesh[@]}" checkpoint.chk_int=80
    for mode in production 0 1 2 3;do
        run "dynamic_p${periodic}_m${mode}" "$mode" "${mesh[@]}" verification.ddf_reference_checkpoint="$OUT/dynamic_p${periodic}_frozen/chk00000080"
    done
done
# 真实FLOAT三层A5 B布局，固定网格隔离碰撞因素；历史checkpoint只读。
SOURCE="$CASE_DIR/runs/a5_threshold_128000/job621436/B/chk00128000"
cp "$SOURCE/Header" "$OUT/restart_Header";cp "$SOURCE/Lattice" "$OUT/restart_Lattice"
prod=(amr.n_cell=128 128 128 amr.max_level=2 amr.max_grid_size=128 256 512
    amr.regrid_int=-1 checkpoint.begin_step=128000 "checkpoint.chk_prefix=$SOURCE/../chk"
    max_step=128032 verification.osi_seed_pattern=false performance.report_int=16
    lbm.err=0.125 0.25 0.5 1 amr.plot_int=32)
# 本地只读链接提供restart入口，新checkpoint写本轮目录，不会回写历史数据。
mkdir "$OUT/local_restart_links"
ln -s "$SOURCE" "$OUT/local_restart_links/chk00128000"
run real_reference_write frozen "${prod[@]}" checkpoint.chk_prefix="$OUT/local_restart_links/chk" checkpoint.keep_latest_only=false checkpoint.chk_int=32
for mode in production 0 1 2 3;do
    run "real_m${mode}" "$mode" "${prod[@]}" verification.ddf_reference_checkpoint="$OUT/local_restart_links/chk00128032"
done
"$REPO_DIR/.venv/bin/python" "$OUT/analyze.py" "$OUT" --checks-only
# 正式单层路径三轮同节点交替，验证接入性能。
for repeat in 1 2 3;do
    order=(frozen production);[[ "$repeat" == 2 ]] && order=(production frozen)
    for mode in "${order[@]}";do
        run "uniform_perf_${repeat}_${mode}" "$mode" max_step=1200 amr.n_cell=256 256 256 amr.max_grid_size=256 performance.report_int=200 verification.osi_seed_pattern=false verification.final_state_check=false
    done
done
# AMR四因素及正式原路径控制，三轮换序；每次320步，排除首80步。
orders=('production 0 1 2 3' '3 2 1 0 production' '1 2 3 production 0')
for repeat in 1 2 3;do
    read -ra order <<< "${orders[$((repeat-1))]}"
    for mode in "${order[@]}";do
        run "amr_perf_${repeat}_m${mode}" "$mode" "${prod[@]}" max_step=128320 performance.report_int=80 amr.plot_int=-1 verification.final_state_check=false
    done
done
"$REPO_DIR/.venv/bin/python" "$OUT/analyze.py" "$OUT"
printf 'completed\n' > "$OUT/completed.txt"
