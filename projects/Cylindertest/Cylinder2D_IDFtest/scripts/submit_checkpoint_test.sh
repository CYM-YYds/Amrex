#!/bin/bash
# DSUB headers must be in the script passed to dsub; keep them here
#DSUB --job_type cosched
#DSUB -n case_Cylinder2D_IDFtest_chk
#DSUB -A root.huazkjdxmrsgjzdsyshi
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=4
#DSUB -N 1
#DSUB -o logs/submit/%J-out.log
#DSUB -e logs/submit/%J-out.log
#DSUB -l cuda122

set -euo pipefail

# Checkpoint/restart test knobs:
#   CHK_STEP=2000 RESTART_END_STEP=2200 CHK_PREFIX=chk dsub -s ./scripts/submit_checkpoint_test.sh
CHK_STEP="${CHK_STEP:-2000}"
RESTART_END_STEP="${RESTART_END_STEP:-$((CHK_STEP + 200))}"
CHK_PREFIX="${CHK_PREFIX:-chk}"
EXECUTABLE="${EXECUTABLE:-./main2d.gnu.MPI.CUDA.ex}"
INPUT_FILE="${INPUT_FILE:-config/inputs}"

#===========================================================
# 加载环境变量
#===========================================================
source /home/HPCBase/tools/module-5.2.0/init/profile.sh
module use /home/HPCBase/modulefiles/
module purge
module load mpi/hmpi/1.2.0_bs2.4.0_sp1
module load compilers/cuda/12.1.0
module load compilers/gcc/10.3.1

#===========================================================
# 获得 hostfile
#===========================================================
echo "----- print env vars -----"

if [ -n "${CCS_ALLOC_FILE:-}" ]; then
    echo
    ls -la "${CCS_ALLOC_FILE}"
    echo "------ cat ${CCS_ALLOC_FILE}"
    cat "${CCS_ALLOC_FILE}"
fi

export HOSTFILE="/tmp/hostfile.$$"
rm -f "${HOSTFILE}"
touch "${HOSTFILE}"

ntask=$(awk -v fff="${HOSTFILE}" '
{
    split($0, a, " ")
    if (length(a[1]) > 0 && length(a[3]) > 0) {
        print a[1]" slots="a[2] >> fff
        total_task += a[3]
    }
}
END { print total_task }
' "${CCS_ALLOC_FILE}")

echo "openmpi hostfile ${HOSTFILE} generated:"
echo "-----------------------"
cat "${HOSTFILE}"
echo "-----------------------"
echo "Total tasks is ${ntask}"

NGPUS_PER_NODE=1
if [ -n "${CUDA_VISIBLE_DEVICES:-}" ]; then
    NGPUS_PER_NODE=$(echo "${CUDA_VISIBLE_DEVICES}" | awk -F',' '{print NF}')
elif command -v nvidia-smi >/dev/null 2>&1; then
    NGPUS_PER_NODE=$(nvidia-smi -L 2>/dev/null | wc -l)
fi
if [ -z "${NGPUS_PER_NODE}" ] || [ "${NGPUS_PER_NODE}" -lt 1 ]; then
    NGPUS_PER_NODE=1
fi

echo "Detected GPUs per node: ${NGPUS_PER_NODE}"

run_case() {
    local phase_name="$1"
    shift

    echo
    echo "========== ${phase_name} =========="
    echo "Command overrides: $*"

    mpirun \
        -hostfile "${HOSTFILE}" \
        -npernode "${NGPUS_PER_NODE}" \
        -x PATH -x LD_LIBRARY_PATH \
        --mca plm_rsh_agent /opt/batch/agent/tools/dstart \
        bash -lc "export CUDA_VISIBLE_DEVICES=\${OMPI_COMM_WORLD_LOCAL_RANK:-\${MPI_LOCALRANKID:-0}}; exec ${EXECUTABLE} ${INPUT_FILE} $*"
}

CHK_DIR=$(printf "%s%08d" "${CHK_PREFIX}" "${CHK_STEP}")

echo "Checkpoint test configuration:"
echo "  EXECUTABLE        = ${EXECUTABLE}"
echo "  INPUT_FILE        = ${INPUT_FILE}"
echo "  CHK_STEP          = ${CHK_STEP}"
echo "  RESTART_END_STEP  = ${RESTART_END_STEP}"
echo "  CHK_PREFIX        = ${CHK_PREFIX}"
echo "  CHK_DIR           = ${CHK_DIR}"

rm -rf "${CHK_DIR}"

run_case \
    "PHASE 1: cold start -> write checkpoint" \
    "max_step=${CHK_STEP}" \
    "checkpoint.begin_step=0" \
    "checkpoint.chk_int=${CHK_STEP}" \
    "checkpoint.chk_prefix=${CHK_PREFIX}" \
    "checkpoint.keep_latest_only=true" \
    "checkpoint.write_particles=true"

if [ ! -f "${CHK_DIR}/Header" ]; then
    echo "[ERROR] Expected checkpoint '${CHK_DIR}/Header' was not generated."
    exit 1
fi

run_case \
    "PHASE 2: restart from checkpoint" \
    "max_step=${RESTART_END_STEP}" \
    "checkpoint.begin_step=${CHK_STEP}" \
    "checkpoint.chk_int=-1" \
    "checkpoint.chk_prefix=${CHK_PREFIX}" \
    "checkpoint.keep_latest_only=true" \
    "checkpoint.write_particles=true"

echo
echo "Checkpoint restart test finished successfully."
