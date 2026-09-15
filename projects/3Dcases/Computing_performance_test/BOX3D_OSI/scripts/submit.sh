#!/bin/bash
# DSUB headers must be in the script passed to dsub; keep them here
#DSUB --job_type cosched
#DSUB -n box3d
#DSUB -A root.iosoeqkp
#DSUB -q root.default
#DSUB -R cpu=32;mem=49152;gpu=1
#DSUB -N 1
#DSUB -o logs/submit/%J-out.log
#DSUB -e logs/submit/%J-out.log
#DSUB -l cuda122

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SHARED_DIR="/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/scripts"

# 每次作业在独立目录中运行，使输入快照、输出和日志始终一一对应。
SOURCE_INPUTS="${CASE_DIR}/config/inputs"
APP_EXE="${CASE_DIR}/main3d.gnu.TPROF.MPI.CUDA.ex"
if [[ ! -f "${SOURCE_INPUTS}" ]]; then
    echo "ERROR: inputs not found: ${SOURCE_INPUTS}" >&2
    exit 1
fi
if [[ ! -x "${APP_EXE}" ]]; then
    echo "ERROR: executable not found: ${APP_EXE}" >&2
    exit 1
fi

RUN_STAMP="$(date +%Y%m%d_%H%M%S)"
RUN_ID="${RUN_STAMP}_job${CCS_JOB_ID:-manual}"
RUN_DIR="${CASE_DIR}/runs/${RUN_ID}"
if [[ -e "${RUN_DIR}" ]]; then
    echo "ERROR: run directory already exists: ${RUN_DIR}" >&2
    exit 1
fi
mkdir -p "${RUN_DIR}"

# 冻结当前输入和命令行覆盖；历史运行目录中的这两份记录不得回写。
cp "${SOURCE_INPUTS}" "${RUN_DIR}/inputs"
chmod a-w "${RUN_DIR}/inputs"
printf '%s\n' "${AMREX_RUN_ARGS:-}" > "${RUN_DIR}/overrides.txt"
printf '%q ' "${APP_EXE}" inputs > "${RUN_DIR}/command.txt"
printf '%s\n' "${AMREX_RUN_ARGS:-}" >> "${RUN_DIR}/command.txt"

{
    echo "run_id=${RUN_ID}"
    echo "job_id=${CCS_JOB_ID:-manual}"
    echo "created_at=$(date --iso-8601=seconds)"
    echo "source_inputs=${SOURCE_INPUTS}"
    echo "git_commit=$(git -C "${CASE_DIR}" rev-parse HEAD 2>/dev/null || echo unknown)"
    if [[ -n "$(git -C "${CASE_DIR}" status --short 2>/dev/null)" ]]; then
        echo "git_worktree=dirty"
    else
        echo "git_worktree=clean"
    fi
    echo "executable=${APP_EXE}"
    echo "executable_sha256=$(sha256sum "${APP_EXE}" | awk '{print $1}')"
} > "${RUN_DIR}/manifest.txt"

# 公共脚本仍按相对路径启动程序，因此在运行目录放置只读符号链接。
ln -s "${APP_EXE}" "${RUN_DIR}/main3d.gnu.TPROF.MPI.CUDA.ex"
cd "${RUN_DIR}"
export AMREX_INPUT_FILE=inputs
export AMREX_RUN_ARGS="${AMREX_RUN_ARGS:-}"

echo "BOX3D_OSI run directory: ${RUN_DIR}"
"${SHARED_DIR}/submit.sh" "$@" > >(tee run.log) 2>&1
