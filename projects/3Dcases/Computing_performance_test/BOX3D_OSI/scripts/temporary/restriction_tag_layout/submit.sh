#!/usr/bin/env bash
# 本次标签存储 A/B 实验；保留用于复现，实验不再需要时可删除。
#DSUB --job_type cosched
#DSUB -n restriction_tags
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=16;mem=49152;gpu=2
#DSUB -N 1
#DSUB -o logs/submit/%J-restriction-tags.log
#DSUB -e logs/submit/%J-restriction-tags.log
#DSUB -l cuda122
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
CASE=/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI
EXP="$CASE/runs/temporary/restriction_tag_layout"
RUN="$EXP/job${CCS_JOB_ID:?}"
mkdir "$RUN"
awk '{if(length($1)>0 && length($2)>0) print $1 " slots=" $2}' "$CCS_ALLOC_FILE" > "$RUN/hostfile"
nvidia-smi > "$RUN/gpu.txt"
sha256sum "$EXP"/{baseline,split}/main3d.gnu.TPROF.MPI.CUDA.ex > "$RUN/executable.sha256"
launch() {
    local ranks="$1"; shift
    mpirun -hostfile "$RUN/hostfile" -n "$ranks" -npernode "$ranks" \
        -x PATH -x LD_LIBRARY_PATH --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
        bash -c 'export CUDA_VISIBLE_DEVICES=${OMPI_COMM_WORLD_LOCAL_RANK:-0}; exec "$@"' bash "$@"
}
common=(amr.n_cell=64 64 64 amr.max_level=2 amr.max_grid_size=32 32 32
    amr.blocking_factor_x=16 16 16 amr.blocking_factor_y=16 16 16 amr.blocking_factor_z=16 16 16
    amr.regrid_int=32 amr.plot_int=-1 checkpoint.write_particles=false
    verification.convergence_enabled=false lbm.osi_mpi_device_direct=0
    performance.osi_detail_timing=0)
# 固定相同输入，检查动态重构后的逐值 DDF 及首次界面平均快照。
for ranks in ${TAG_RANKS:-2}; do
    for variant in baseline split; do
        dir="$RUN/check_r${ranks}_${variant}"
        mkdir "$dir"; cd "$dir"
        args=("${common[@]}" max_step=64 performance.report_int=32 checkpoint.chk_int=64 verification.average_probe_first=true)
        if [[ "$variant" == split ]]; then
            args+=("verification.ddf_reference_checkpoint=$RUN/check_r${ranks}_baseline/chk00000064")
        fi
        printf '%q ' "$EXP/$variant/main3d.gnu.TPROF.MPI.CUDA.ex" "$EXP/baseline/config/inputs" "${args[@]}" > command.txt
        launch "$ranks" "$EXP/$variant/main3d.gnu.TPROF.MPI.CUDA.ex" "$EXP/baseline/config/inputs" "${args[@]}" > run.log 2>&1
        if [[ "$variant" == split ]]; then
            awk '/^ddf_cell_norm_level:/ {for(i=1;i<=NF;i++) if($i~/^valid_linf=/){split($i,a,"="); n++; if(a[2]+0!=0)bad=1}} END{exit(bad||n!=3)}' run.log
            for p in average_probe_*.bin average_probe_*.mask; do cmp "$p" "$RUN/check_r${ranks}_baseline/$p"; done
        fi
    done
    echo "PASS numerical ranks=$ranks"
    # 同一节点/GPU 分配下 ABBA 两轮；排除前 64 步的窗口。
    for trial in 0 1 2 3 4 5 6 7; do
        case "$trial" in 0|3|4|7) variant=baseline;; *) variant=split;; esac
        dir="$RUN/perf_r${ranks}_${trial}_${variant}"
        mkdir "$dir"; cd "$dir"
        args=("${common[@]}" max_step=256 performance.report_int=64 checkpoint.chk_int=-1)
        printf '%q ' "$EXP/$variant/main3d.gnu.TPROF.MPI.CUDA.ex" "$EXP/baseline/config/inputs" "${args[@]}" > command.txt
        launch "$ranks" "$EXP/$variant/main3d.gnu.TPROF.MPI.CUDA.ex" "$EXP/baseline/config/inputs" "${args[@]}" > run.log 2>&1
        echo "PASS timing ranks=$ranks trial=$trial variant=$variant"
    done
done
echo "PASS all $RUN"
REPO=/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex
"$REPO/.venv/bin/python" "$CASE/scripts/temporary/restriction_tag_layout/analyze.py" "$RUN" > "$RUN/analysis.log"
