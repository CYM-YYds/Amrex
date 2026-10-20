#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n lbm_particle_geometry
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=16;mem=49152;gpu=2
#DSUB -N 1
#DSUB -o logs/submit/%J-particle-geometry.log
#DSUB -e logs/submit/%J-particle-geometry.log
#DSUB -l cuda122
# 静态圆盘容器、MPI、布局变化、读写和纯流体回归；不验证耦合动力学。
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="$(git -C "$CASE_DIR" rev-parse --show-toplevel)"
RUN_DIR="$CASE_DIR/runs/particle_geometry_${CCS_JOB_ID:?}"
mkdir -p "$RUN_DIR"
cp "$CASE_DIR"/main{2,3}d.gnu.TPROF.MPI.CUDA.ex "$RUN_DIR/"
cp "${PARTICLE_BASELINE_EXE:-$CASE_DIR/runs/particle_port_baseline_20261016/app3d.ex}" "$RUN_DIR/baseline3d.ex"
cp -a "$CASE_DIR/src" "$CASE_DIR/config" "$RUN_DIR/"
cp "$CASE_DIR/tests/submit_particle_geometry_validation.sh" "$CASE_DIR/tests/check_particle_geometry.py" "$RUN_DIR/"
git -C "$REPO_DIR" diff -- "$CASE_DIR" > "$RUN_DIR/source.diff"
sha256sum "$RUN_DIR"/*.ex > "$RUN_DIR/executable.sha256"
awk 'NF >= 2 {print $1 " slots=" $2}' "${CCS_ALLOC_FILE:?}" > "$RUN_DIR/hostfile"
printf 'case\texit_code\n' > "$RUN_DIR/status.tsv"
run() {
    local name=$1 dim=$2 ranks=$3 exe=$4
    shift 4
    mkdir -p "$RUN_DIR/$name"
    cd "$RUN_DIR/$name"
    local input="$RUN_DIR/config/inputs"
    [[ "$dim" == 3 ]] || input="$input.2d"
    printf '%q ' "$exe" "$input" "$@" > command.txt
    printf '\n' >> command.txt
    local result=0
    mpirun -hostfile "$RUN_DIR/hostfile" -n "$ranks" -npernode "$ranks" \
        -x PATH -x LD_LIBRARY_PATH --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
        /usr/bin/env -u BASH_ENV bash --noprofile --norc -c \
        'export CUDA_VISIBLE_DEVICES=${OMPI_COMM_WORLD_LOCAL_RANK:-0}; exec "$@"' bash \
        "$exe" "$input" "$@" > run.log 2>&1 || result=$?
    printf '%s\t%s\n' "$name" "$result" >> "$RUN_DIR/status.tsv"
    echo "PARTICLE_CASE $name exit=$result"
}
app2="$RUN_DIR/main2d.gnu.TPROF.MPI.CUDA.ex"
app3="$RUN_DIR/main3d.gnu.TPROF.MPI.CUDA.ex"
common=(amr.regrid_int=32 amr.plot_int=32 checkpoint.chk_int=96 checkpoint.keep_latest_only=false
    verification.convergence_enabled=false verification.final_state_check=true
    performance.report_int=192 lbm.osi_mpi_pipeline_chunk_bytes=0)
mesh2=(amr.n_cell=64 64 amr.max_level=2 amr.max_grid_size=32 32 32
    amr.blocking_factor_x=16 16 16 amr.blocking_factor_y=16 16 16 amr.n_proper=2)
for ranks in 1 2; do
    run "disk_r$ranks" 2 "$ranks" "$app2" "${common[@]}" "${mesh2[@]}" max_step=192 \
        checkpoint.write_particles=true verification.particle_checksum=true verification.regrid_fixture=true
 done
run disk_resume 2 1 "$app2" "${common[@]}" "${mesh2[@]}" max_step=192 \
    checkpoint.write_particles=true verification.particle_checksum=true verification.regrid_fixture=true \
    checkpoint.begin_step=96 checkpoint.chk_prefix="$RUN_DIR/disk_r2/chk" checkpoint.chk_int=-1 \
    verification.ddf_reference_checkpoint="$RUN_DIR/disk_r2/chk00000192"
run fluid_reference 2 2 "$app2" "${common[@]}" "${mesh2[@]}" max_step=192 amr.plot_int=-1 \
    checkpoint.write_particles=false verification.regrid_fixture=true \
    verification.ddf_reference_checkpoint="$RUN_DIR/disk_r2/chk00000192"
run reject_sphere 2 1 "$app2" "${common[@]}" "${mesh2[@]}" max_step=1 particles.shape=sphere checkpoint.write_particles=true
cp -a "$RUN_DIR/disk_r2/chk00000096" "$RUN_DIR/wrong00000096"
sed -i 's/2 disk/2 sphere/' "$RUN_DIR/wrong00000096/particles_0/Geometry"
run reject_metadata 2 1 "$app2" "${common[@]}" "${mesh2[@]}" max_step=97 \
    checkpoint.write_particles=true checkpoint.begin_step=96 checkpoint.chk_prefix="$RUN_DIR/wrong"
cp -a "$RUN_DIR/disk_r2/chk00000096" "$RUN_DIR/missing00000096"
mv "$RUN_DIR/missing00000096/particles_0/Geometry" "$RUN_DIR/missing_geometry.saved"
run reject_legacy_sphere 2 1 "$app2" "${common[@]}" "${mesh2[@]}" max_step=97 \
    checkpoint.write_particles=true checkpoint.begin_step=96 checkpoint.chk_prefix="$RUN_DIR/missing"
mesh3=(amr.n_cell=64 64 64 amr.max_level=0 amr.max_grid_size=32
    amr.blocking_factor_x=16 amr.blocking_factor_y=16 amr.blocking_factor_z=16)
run sphere_old 3 1 "$RUN_DIR/baseline3d.ex" "${common[@]}" "${mesh3[@]}" max_step=4 \
    amr.plot_int=-1 checkpoint.chk_int=2 checkpoint.write_particles=true verification.particle_checksum=true
run sphere_current 3 1 "$app3" "${common[@]}" "${mesh3[@]}" max_step=4 \
    amr.plot_int=2 checkpoint.chk_int=2 checkpoint.write_particles=true verification.particle_checksum=true \
    verification.ddf_reference_checkpoint="$RUN_DIR/sphere_old/chk00000004"
run sphere_old_restart 3 2 "$app3" "${common[@]}" "${mesh3[@]}" max_step=4 \
    amr.plot_int=2 checkpoint.chk_int=-1 checkpoint.write_particles=true verification.particle_checksum=true \
    checkpoint.begin_step=2 checkpoint.chk_prefix="$RUN_DIR/sphere_old/chk" \
    verification.ddf_reference_checkpoint="$RUN_DIR/sphere_old/chk00000004"
"$REPO_DIR/.venv/bin/python" "$RUN_DIR/check_particle_geometry.py" "$RUN_DIR"
