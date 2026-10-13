#!/usr/bin/env bash
#DSUB --job_type cosched
#DSUB -n box3d_sparse_initialization
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=16;mem=49152;gpu=2
#DSUB -N 1
#DSUB -o logs/submit/%J-sparse-initialization.log
#DSUB -e logs/submit/%J-sparse-initialization.log
#DSUB -l cuda122
set -euo pipefail
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1 compilers/cuda/12.8.0 compilers/gcc/11.3.0
TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASE_DIR="$(cd "${TEST_DIR}/.." && pwd)"
source "${CASE_DIR}/scripts/osi_experiment_common.sh"
RUN_DIR="${CASE_DIR}/runs/sparse_initialization_${CCS_JOB_ID:?}"
mkdir "${RUN_DIR}"
cp "${SPARSE_INIT_TEST_EXE:?指定独立初始化测试可执行文件}" "${RUN_DIR}/test.ex"
cp "${CASE_DIR}/main3d.gnu.TPROF.MPI.CUDA.ex" "${RUN_DIR}/after.ex"
cp "${SPARSE_INIT_BASELINE_EXE:?指定修改前可执行文件}" "${RUN_DIR}/before.ex"
cp "${CASE_DIR}/config/inputs" "${RUN_DIR}/inputs"
git -C "${CASE_DIR}" rev-parse HEAD > "${RUN_DIR}/git_head.txt"
git -C "${CASE_DIR}" diff -- src tests > "${RUN_DIR}/source.diff"
cp "${TEST_DIR}"/sparse_initialization_test.cpp "${TEST_DIR}"/SparseInitialization.GNUmakefile "${RUN_DIR}/"
cp "$(dirname "${SPARSE_INIT_TEST_EXE}")/sparse_initialization_impl.inc" "${RUN_DIR}/"
sha256sum "${RUN_DIR}"/*.ex > "${RUN_DIR}/executable.sha256"
osi_write_hostfile "${RUN_DIR}/hostfile"
cd "${RUN_DIR}"
for ranks in 1 2; do
    osi_launch_mpi hmpi "${RUN_DIR}/hostfile" "${ranks}" "${ranks}" \
        "${RUN_DIR}/test.ex" amrex.abort_on_unused_inputs=0 > "${RUN_DIR}/unit_${ranks}.log" 2>&1
    grep -F "PASS sparse initialization matrix ranks=${ranks}" "${RUN_DIR}/unit_${ranks}.log"
done
# 父层 valid 模板有缺口时，必须在启动插值前给出定位并终止。
if osi_launch_mpi hmpi "${RUN_DIR}/hostfile" 2 2 "${RUN_DIR}/test.ex" \
    coverage_fail=1 > "${RUN_DIR}/coverage_fail.log" 2>&1; then
    echo 'ERROR: missing parent coverage did not abort' >&2
    exit 1
fi
for expected in '[InterpolationCoverage]' 'missing_coarse_boxes=' 'fine_level=1'; do
    grep -Fq "${expected}" "${RUN_DIR}/coverage_fail.log"
done
echo 'PASS initialization rejects missing parent stencil'
# 与同布局、同 rank 的旧执行文件比较，动态 AMR 的共享 ghost 路径也纳入回归。
for mode in 0 1 2; do
    ranks_list=(2)
    if [[ "${mode}" == 0 ]]; then ranks_list=(1 2); fi
    for ranks in "${ranks_list[@]}"; do
        for version in before after; do
            name="${version}_r${ranks}_i${mode}"
            mkdir "${RUN_DIR}/${name}"
            cd "${RUN_DIR}/${name}"
            extra=()
            if [[ "${version}" == after ]]; then
                extra+=("verification.ddf_reference_checkpoint=${RUN_DIR}/before_r${ranks}_i${mode}/chk00000064")
            fi
            args=(max_step=64 performance.report_int=64
                amr.n_cell=64 64 64 amr.max_level=2 amr.max_grid_size=32 32 32
                amr.blocking_factor_x=16 16 16 amr.blocking_factor_y=16 16 16
                amr.blocking_factor_z=16 16 16 amr.regrid_int=32 amr.plot_int=-1
                checkpoint.chk_int=64 checkpoint.write_particles=false
                verification.convergence_enabled=false verification.osi_seed_pattern=false
                verification.osi_ab_check=true verification.osi_ab_continue_on_mismatch=false
                verification.osi_step_entry_check_interval=1 verification.osi_check_after_average_valid=true
                verification.check_after_first_fill=true lbm.stream_mode=1 lbm.interp_mode="${mode}"
                lbm.osi_mpi_device_direct=0 lbm.osi_mpi_pipeline_chunk_bytes=0)
            osi_record_command command.txt "${RUN_DIR}/${version}.ex" "${RUN_DIR}/inputs" "${args[@]}" "${extra[@]}"
            osi_launch_mpi hmpi "${RUN_DIR}/hostfile" "${ranks}" "${ranks}" \
                "${RUN_DIR}/${version}.ex" "${RUN_DIR}/inputs" "${args[@]}" "${extra[@]}" > run.log 2>&1
            if [[ "${version}" == after ]]; then
                awk '/^ddf_cell_norm_level:/ {
                    for (i=1;i<=NF;++i) if ($i ~ /^valid_linf=/) {
                        split($i,a,"="); ++levels; if (a[2]+0>2e-14) failed=1;
                    }
                } END { exit(failed || levels!=3) }' run.log
            fi
            echo "PASS dynamic AMR ${name} steps=64"
        done
    done
done
echo "PASS sparse initialization validation: ${RUN_DIR}"
