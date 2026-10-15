#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n box3d_cpc_cache_validation
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=16;mem=49152;gpu=2
#DSUB -N 1
#DSUB -o logs/submit/%J-cpc-cache-validation.log
#DSUB -e logs/submit/%J-cpc-cache-validation.log
#DSUB -l cuda122
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASE_DIR="$(cd "${TEST_DIR}/.." && pwd)"
RUN_DIR="${CASE_DIR}/runs/cpc_cache_${CCS_JOB_ID:?}"
mkdir "${RUN_DIR}"
cp "${CPC_CACHE_TEST_EXE:?指定独立通信缓存测试可执行文件}" "${RUN_DIR}/test.ex"
cp "${CASE_DIR}/main3d.gnu.TPROF.MPI.CUDA.ex" "${RUN_DIR}/after.ex"
cp "${CASE_DIR}/config/inputs" "${RUN_DIR}/inputs"
git -C "${CASE_DIR}" rev-parse HEAD > "${RUN_DIR}/git_head.txt"
git -C "${CASE_DIR}" diff -- src tests > "${RUN_DIR}/source.diff"
if [[ -n "${CPC_CACHE_BASELINE_EXE:-}" ]]; then
    cp "${CPC_CACHE_BASELINE_EXE}" "${RUN_DIR}/before.ex"
fi
sha256sum "${RUN_DIR}"/*.ex > "${RUN_DIR}/executable.sha256"
awk '{ if (length($1)>0 && length($2)>0) print $1 " slots=" $2 }' \
    "${CCS_ALLOC_FILE:?}" > "${RUN_DIR}/hostfile"
launch() {
    local ranks="$1"
    shift
    mpirun -hostfile "${RUN_DIR}/hostfile" -n "${ranks}" -npernode "${ranks}" \
        -x PATH -x LD_LIBRARY_PATH \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
        bash -lc 'export CUDA_VISIBLE_DEVICES=${OMPI_COMM_WORLD_LOCAL_RANK:-${MPI_LOCALRANKID:-0}}; exec "$@"' bash "$@"
}
for ranks in 1 2; do
    launch "${ranks}" "${RUN_DIR}/test.ex" > "${RUN_DIR}/cache_${ranks}.log" 2>&1
    grep -F "PASS CPC cache ranks=${ranks}" "${RUN_DIR}/cache_${ranks}.log"
done
# 基线存在时，对同一布局、输入和 rank 数做改动前后的全 valid DDF 对照。
if [[ -f "${RUN_DIR}/before.ex" ]]; then
    run_case() {
        local version="$1" ranks="$2" mode="$3" steps="$4"
        local name="${version}_r${ranks}_i${mode}"
        mkdir "${RUN_DIR}/${name}"
        cd "${RUN_DIR}/${name}"
        local extra=()
        if [[ "${version}" == after ]]; then
            extra+=("verification.ddf_reference_checkpoint=${RUN_DIR}/before_r${ranks}_i${mode}/chk$(printf '%08d' "${steps}")")
        fi
        local args=(max_step="${steps}" performance.report_int="${steps}"
            amr.n_cell=64 64 64 amr.max_level=2 amr.max_grid_size=32 32 32
            amr.blocking_factor_x=16 16 16 amr.blocking_factor_y=16 16 16
            amr.blocking_factor_z=16 16 16 amr.regrid_int=32 amr.plot_int=-1
            checkpoint.chk_int="${steps}" checkpoint.write_particles=false
            verification.convergence_enabled=false verification.osi_seed_pattern=false
            verification.osi_ab_check=true verification.osi_ab_continue_on_mismatch=true
            verification.osi_step_entry_check_interval=1 verification.osi_check_after_average_valid=true
            verification.check_after_first_fill=true verification.average_probe_first=true
            lbm.stream_mode=1 lbm.interp_mode="${mode}" lbm.osi_mpi_device_direct=0
            lbm.osi_mpi_pipeline_chunk_bytes=0)
        printf '%q ' "${RUN_DIR}/${version}.ex" "${RUN_DIR}/inputs" "${args[@]}" "${extra[@]}" > command.txt
        printf '\n' >> command.txt
        launch "${ranks}" "${RUN_DIR}/${version}.ex" "${RUN_DIR}/inputs" \
            "${args[@]}" "${extra[@]}" > run.log 2>&1
        if [[ "${version}" == after ]]; then
            awk '
                /^ddf_cell_norm_level:/ {
                    for (i=1;i<=NF;++i) if ($i ~ /^valid_linf=/) {
                        split($i,a,"="); ++levels; if (a[2]+0 != 0) failed=1;
                    }
                }
                END { exit(failed || levels!=3) }
            ' run.log
            for probe in average_probe_*.bin average_probe_*.mask; do
                cmp "$probe" "${RUN_DIR}/before_r${ranks}_i${mode}/$probe"
            done
        fi
        echo "PASS execution ${name} steps=${steps}"
    }
    IFS=, read -ra modes <<< "${CPC_CACHE_MODES:-0,1,2}"
    for mode in "${modes[@]}"; do
        case "${mode}" in
            0) ranks_to_test=(1 2) ;;
            1|2) ranks_to_test=(2) ;;
            *) echo "ERROR: unsupported interpolation mode ${mode}" >&2; exit 1 ;;
        esac
        for ranks in "${ranks_to_test[@]}"; do
            for version in before after; do run_case "${version}" "${ranks}" "${mode}" 64; done
        done
    done
fi
echo "CPC cache validation completed: ${RUN_DIR}"
