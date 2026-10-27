#!/usr/bin/env bash
# GUI服务限定编译节点；多进程必须使用同一安装中的MPI构建与启动器。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
if [[ "$(hostname -s)" != whshare-agent-1 ]]; then
    echo 'ERROR: pvserver must run on whshare-agent-1; refusing this node.' >&2
    exit 1
fi
PV_PREFIX="${PARAVIEW_SERVER_PREFIX:-${REPO_DIR}/.tools/paraview-mpi/env}"
PROCESSES=1
SERVER_ARGS=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --processes)
            [[ $# -ge 2 ]] || { echo 'ERROR: --processes needs a value.' >&2; exit 2; }
            PROCESSES="$2"; shift 2 ;;
        --prefix)
            [[ $# -ge 2 ]] || { echo 'ERROR: --prefix needs a value.' >&2; exit 2; }
            PV_PREFIX="$2"; shift 2 ;;
        *) SERVER_ARGS+=("$1"); shift ;;
    esac
done
if [[ ! "$PROCESSES" =~ ^[1-9][0-9]{0,2}$ ]] || (( PROCESSES > 256 )); then
    echo 'ERROR: processes must be an integer in [1,256].' >&2
    exit 2
fi
if [[ "$PV_PREFIX" != /* ]]; then
    echo 'ERROR: ParaView prefix must be an absolute path.' >&2
    exit 2
fi
PVSERVER="${PV_PREFIX}/bin/pvserver"
if [[ ! -x "$PVSERVER" ]]; then
    echo "ERROR: pvserver not found in ${PV_PREFIX}; select an installed ParaView prefix." >&2
    exit 1
fi
# 不继承系统MPI、Python或图形插件路径；启动器和服务必须来自相同安装。
CLEAN_ENV=(env -u LD_LIBRARY_PATH -u PYTHONPATH -u PYTHONHOME -u DISPLAY
    -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH -u PV_PLUGIN_PATH
    QT_QPA_PLATFORM=offscreen "PATH=${PV_PREFIX}/bin:${PATH}")
SERVER_COMMAND=("${CLEAN_ENV[@]}" "$PVSERVER" --force-offscreen-rendering "${SERVER_ARGS[@]}")
if (( PROCESSES > 1 )); then
    MPIEXEC="${PV_PREFIX}/bin/mpiexec"
    if [[ ! -x "$MPIEXEC" ]]; then
        echo "ERROR: ${PV_PREFIX} has no matching bin/mpiexec; the current serial environment cannot run multiple ranks." >&2
        exit 1
    fi
    # 优先读取安装导出的构建信息；无CMake配置的二进制包用其pvpython探测控制器。
    shopt -s nullglob
    CONFIGS=("${PV_PREFIX}"/lib/cmake/paraview-*/paraview-config.cmake
             "${PV_PREFIX}"/lib64/cmake/paraview-*/paraview-config.cmake)
    if (( ${#CONFIGS[@]} > 0 )); then
        if ! grep -Eq '^[[:space:]]*set\(PARAVIEW_USE_MPI[[:space:]]+"?(ON|TRUE|1)"?\)' "${CONFIGS[@]}"; then
            echo 'ERROR: selected ParaView was built without MPI; do not launch independent serial servers with mpiexec.' >&2
            exit 1
        fi
    elif [[ -x "${PV_PREFIX}/bin/pvpython" ]]; then
        CONTROLLER=$("${CLEAN_ENV[@]}" "${PV_PREFIX}/bin/pvpython" -c 'from paraview import servermanager; print(servermanager.vtkProcessModule.GetProcessModule().GetGlobalController().GetClassName())')
        if ! grep -Fxq vtkMPIController <<< "$CONTROLLER"; then
            echo 'ERROR: selected ParaView does not expose an MPI controller.' >&2
            exit 1
        fi
    else
        echo 'ERROR: cannot verify MPI support: missing ParaView CMake config and pvpython.' >&2
        exit 1
    fi
    # 版本探测不创建多rank服务；能力检查仍在探测前执行。
    if [[ " ${SERVER_ARGS[*]} " == *' --version '* ]]; then
        exec "${CLEAN_ENV[@]}" "$PVSERVER" --version
    fi
    SERVER_COMMAND=("${CLEAN_ENV[@]}" "$MPIEXEC" -n "$PROCESSES" "$PVSERVER" --force-offscreen-rendering "${SERVER_ARGS[@]}")
fi
# 服务日志保存在共享仓库；窗口关闭后仍可查看，不把 tee 的退出码当作服务退出码。
# 版本预检不创建日志；正式服务继续使用 exec，保留原来的退出码与信号路径。
if [[ " ${SERVER_ARGS[*]} " != *' --version '* ]]; then
    LOG_DIR="${PARAVIEW_SERVER_LOG_DIR:-${REPO_DIR}/.tools/paraview-mpi/logs}"
    mkdir -p -- "$LOG_DIR"
    LOG_FILE=$(mktemp "${LOG_DIR}/pvserver_$(date +%Y%m%d_%H%M%S)_XXXXXX.log")
    exec > >(tee -- "$LOG_FILE") 2>&1
    printf 'ParaView server log: %s\nHost: %s; processes: %s; prefix: %s\n' \
        "$LOG_FILE" "$(hostname -s)" "$PROCESSES" "$PV_PREFIX"
fi
exec "${SERVER_COMMAND[@]}"
