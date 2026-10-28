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
DIM="${DIM:-3}"
[[ "$DIM" == 2 || "$DIM" == 3 ]] || { echo "错误: DIM 必须为 2 或 3" >&2; exit 1; }
INPUT_NAME=inputs
if [[ "$DIM" == 2 ]]; then INPUT_NAME=inputs.2d; fi
PRECISION="${PRECISION:-FLOAT}"
[[ "$PRECISION" == DOUBLE || "$PRECISION" == FLOAT ]] || { echo "错误: PRECISION 必须为 DOUBLE 或 FLOAT" >&2; exit 1; }
PRECISION_SUFFIX=""
if [[ "$PRECISION" == FLOAT ]]; then PRECISION_SUFFIX=.FLOAT; fi
export BOX3D_DIM="$DIM"
SOURCE_INPUTS="${CASE_DIR}/config/${INPUT_NAME}"
APP_EXE="${CASE_DIR}/main${DIM}d.gnu${PRECISION_SUFFIX}.TPROF.MPI.CUDA.ex"
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

# 续跑时只把历史 checkpoint 作为只读入口链接到新运行目录；后续 checkpoint
# 仍使用本地 chk 前缀写出，避免覆盖或清理历史运行目录中的源数据。
RESTART_CHECKPOINT="${RESTART_CHECKPOINT:-}"
if [[ -n "${RESTART_CHECKPOINT}" ]]; then
    RESTART_CHECKPOINT="$(realpath "${RESTART_CHECKPOINT}")"
    if [[ ! -f "${RESTART_CHECKPOINT}/Header" ]]; then
        echo "ERROR: restart checkpoint Header not found: ${RESTART_CHECKPOINT}/Header" >&2
        exit 1
    fi
    RESTART_STEP="$(sed -n '3p' "${RESTART_CHECKPOINT}/Header")"
    if [[ ! "${RESTART_STEP}" =~ ^[0-9]+$ ]]; then
        echo "ERROR: invalid restart step in ${RESTART_CHECKPOINT}/Header: ${RESTART_STEP}" >&2
        exit 1
    fi
    RESTART_LINK="${RUN_DIR}/chk$(printf '%08d' "${RESTART_STEP}")"
    ln -s "${RESTART_CHECKPOINT}" "${RESTART_LINK}"
    AMREX_RUN_ARGS="${AMREX_RUN_ARGS:-} checkpoint.begin_step=${RESTART_STEP} checkpoint.chk_prefix=chk"
fi

# 冻结当前输入和命令行覆盖；历史运行目录中的这两份记录不得回写。
cp "${SOURCE_INPUTS}" "${RUN_DIR}/inputs"
chmod a-w "${RUN_DIR}/inputs"
printf '%s\n' "${AMREX_RUN_ARGS:-}" > "${RUN_DIR}/overrides.txt"
printf '%q ' "${APP_EXE}" inputs > "${RUN_DIR}/command.txt"
printf '%s\n' "${AMREX_RUN_ARGS:-}" >> "${RUN_DIR}/command.txt"

{
    echo "dimension=${DIM}"
    echo "precision=${PRECISION}"
    echo "run_id=${RUN_ID}"
    echo "job_id=${CCS_JOB_ID:-manual}"
    echo "created_at=$(date --iso-8601=seconds)"
    echo "source_inputs=${SOURCE_INPUTS}"
    echo "restart_checkpoint=${RESTART_CHECKPOINT:-none}"
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
ln -s "${APP_EXE}" "${RUN_DIR}/main${DIM}d.gnu.TPROF.MPI.CUDA.ex"
cd "${RUN_DIR}"
export AMREX_EXECUTABLE="${RUN_DIR}/run_rank.sh"
# 运行目录冻结维度，rank 包装器通过本地程序名选择应用。
sed "s/BOX3D_DIM:-3/BOX3D_DIM:-${DIM}/" "${SCRIPT_DIR}/run_rank.sh" > "${RUN_DIR}/run_rank.sh"
chmod +x "${RUN_DIR}/run_rank.sh"
export AMREX_INPUT_FILE=inputs
export AMREX_RUN_ARGS="${AMREX_RUN_ARGS:-}"

echo "BOX3D_OSI run directory: ${RUN_DIR}"
# 应用日志直接写入 run.log；启动器超过完成宽限期仍未退出则按失败回收。
REPO_DIR="$(git -C "${CASE_DIR}" rev-parse --show-toplevel)"
exec "${REPO_DIR}/.venv/bin/python" "${SCRIPT_DIR}/guard_run.py" \
    --wall-seconds "${BOX3D_WALL_SECONDS:-0}" \
    --exit-grace-seconds "${BOX3D_EXIT_GRACE_SECONDS:-60}" \
    -- bash "${SHARED_DIR}/submit.sh" "$@"
