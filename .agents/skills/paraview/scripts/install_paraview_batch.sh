#!/usr/bin/env bash
# 安装ARM64 MPI版ParaView服务与批处理应用，不修改系统环境或用户启动配置。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
APP_DIR="${REPO_DIR}/.tools/paraview-mpi"
# 安装和运行探测也通过编译节点执行，不在登录节点启动 ParaView。
BATCH_HOST="whshare-agent-1"
if [[ "$(hostname -s)" != "${BATCH_HOST%%.*}" ]]; then
    printf -v REMOTE_COMMAND '%q ' bash "${SCRIPT_DIR}/install_paraview_batch.sh" "$@"
    exec env -u LD_LIBRARY_PATH ssh -T -o BatchMode=yes -o ConnectTimeout=10 \
        "${BATCH_HOST}" "${REMOTE_COMMAND}"
fi
if [[ "$(uname -s)" != Linux || "$(uname -m)" != aarch64 ]]; then
    echo 'ERROR: this installer is pinned for Linux aarch64' >&2
    exit 1
fi
mkdir -p "${APP_DIR}/bootstrap"
# 安装固定在同一编译节点，用本地锁防止并发并避开共享文件系统的遗留锁。
exec 9>"/tmp/paraview-mpi-install-${UID}.lock"
flock -n 9 || { echo 'ERROR: another ParaView MPI installation is running.' >&2; exit 1; }
ARCHIVE="${APP_DIR}/bootstrap/micromamba-2.9.0-0.tar.bz2"
URL='https://mirrors.tuna.tsinghua.edu.cn/anaconda/cloud/conda-forge/linux-aarch64/micromamba-2.9.0-0.tar.bz2'
EXPECTED='e705ffeed90ce0659eb546e4b1e1028c9eaf0bc9cc854867b19ac5ce0ba5852f'
if [[ ! -f "${ARCHIVE}" ]] || [[ "$(sha256sum "${ARCHIVE}" | awk '{print $1}')" != "${EXPECTED}" ]]; then
    curl --fail --location --connect-timeout 10 --retry 2 -o "${ARCHIVE}.download" "${URL}"
    printf '%s  %s\n' "${EXPECTED}" "${ARCHIVE}.download" | sha256sum -c -
    mv "${ARCHIVE}.download" "${ARCHIVE}"
fi
# 只解出已校验的单个可执行文件，避免覆盖其他安装内容。
tar -xjf "${ARCHIVE}" -C "${APP_DIR}/bootstrap" bin/micromamba
MAMBA="${APP_DIR}/bootstrap/bin/micromamba"
LOCK="${SCRIPT_DIR}/paraview-mpi-linux-aarch64.lock"
# 以包管理器已有元数据为安装状态来源；锁文件匹配时不重复重链整个环境。
if [[ -x "${APP_DIR}/env/bin/python" && -f "${LOCK}" ]] && \
    "${REPO_DIR}/.venv/bin/python" - "${APP_DIR}/env" "${LOCK}" <<'CHECK_ENV'
import json
from pathlib import Path
import sys
from urllib.parse import unquote
try:
    prefix, lock = map(Path, sys.argv[1:])
    expected = {}
    for line in lock.read_text().splitlines():
        if line.startswith('https://'):
            url, sha256 = line.rsplit('#', 1)
            expected[unquote(url.rsplit('/', 1)[1])] = sha256
    installed = {}
    for path in (prefix / 'conda-meta').glob('*.json'):
        record = json.loads(path.read_text())
        installed[record['fn']] = record['sha256']
    sys.exit(0 if expected and expected == installed else 1)
except (OSError, KeyError, ValueError):
    sys.exit(1)
CHECK_ENV
then
    echo 'Pinned MPI dependency environment already installed; skipping package relink.'
else
    OPERATION=create
    if [[ -f "${APP_DIR}/env/conda-meta/history" ]]; then
        OPERATION=install
    fi
    if [[ -f "${LOCK}" ]]; then
        "${MAMBA}" --no-rc "${OPERATION}" -r "${APP_DIR}/mamba" -p "${APP_DIR}/env" --file "${LOCK}" -y
    else
        "${MAMBA}" --no-rc "${OPERATION}" -r "${APP_DIR}/mamba" -p "${APP_DIR}/env" --override-channels \
            -c https://mirrors.tuna.tsinghua.edu.cn/anaconda/cloud/conda-forge \
            'python=3.12' numpy 'mpich>=4,<5' 'gcc_linux-aarch64>=14' 'gxx_linux-aarch64>=14' \
            libegl-devel libgl-devel libglvnd-devel mesalib -y
    fi
fi

PREFIX="${APP_DIR}/env"
CONFIG="${PREFIX}/lib/cmake/paraview-6.2/paraview-config.cmake"
if [[ -x "${PREFIX}/bin/pvserver" && -x "${PREFIX}/bin/pvbatch" && \
      -x "${PREFIX}/bin/pvpython" && -x "${PREFIX}/bin/mpiexec" && -f "$CONFIG" ]] && \
    grep -Eq 'set\(PARAVIEW_USE_MPI "ON"\)' "$CONFIG"; then
    CONTROLLER=$(env -u LD_LIBRARY_PATH -u PYTHONPATH -u PYTHONHOME \
        "${PREFIX}/bin/pvpython" -c 'from paraview import servermanager; print(servermanager.vtkProcessModule.GetProcessModule().GetGlobalController().GetClassName())')
    [[ "$CONTROLLER" == vtkMPIController ]] || {
        echo "ERROR: MPI controller probe failed: ${CONTROLLER}" >&2
        exit 1
    }
    env -u LD_LIBRARY_PATH -u PYTHONPATH -u PYTHONHOME "${PREFIX}/bin/pvserver" --version
    exit 0
fi
mkdir -p "${APP_DIR}/source"
SOURCE="${APP_DIR}/source/ParaView-v6.2.0.tar.xz"
SOURCE_SHA256='373afd566ef5f6c650c3acf4a5d4f43ad75975537f7fe225cb53ed20fe41cd20'
if [[ ! -f "$SOURCE" ]] || [[ "$(sha256sum "$SOURCE" | awk '{print $1}')" != "$SOURCE_SHA256" ]]; then
    curl --fail --location --connect-timeout 10 --retry 2 -o "${SOURCE}.download" \
        https://www.paraview.org/files/v6.2/ParaView-v6.2.0.tar.xz
    printf '%s  %s\n' "$SOURCE_SHA256" "${SOURCE}.download" | sha256sum -c -
    mv "${SOURCE}.download" "$SOURCE"
fi
# 大量源码和目标小文件放编译节点本地盘，安装结果与日志保留在共享目录。
BUILD_ROOT="${PARAVIEW_BUILD_ROOT:-${TMPDIR:-/tmp}/paraview-mpi-${UID}}"
mkdir -p "$BUILD_ROOT"
if [[ ! -f "${BUILD_ROOT}/source/CMakeLists.txt" ]]; then
    mkdir -p "${BUILD_ROOT}/unpack"
    tar -xJf "$SOURCE" -C "${BUILD_ROOT}/unpack"
    mv "${BUILD_ROOT}/unpack/ParaView-v6.2.0" "${BUILD_ROOT}/source"
fi
# 编译器、Python、MPI和EGL全部从独立前缀解析；不链接待删除的串行环境。
export PATH="${PREFIX}/bin:${PATH}"
unset LD_LIBRARY_PATH PYTHONPATH PYTHONHOME
cmake -S "${BUILD_ROOT}/source" -B "${BUILD_ROOT}/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O2 -DNDEBUG' \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_PREFIX_PATH="$PREFIX" -DCMAKE_INSTALL_RPATH="${PREFIX}/lib" \
    -DCMAKE_BUILD_RPATH="${PREFIX}/lib" \
    -DCMAKE_EXE_LINKER_FLAGS="-Wl,-rpath-link,${PREFIX}/lib" \
    -DCMAKE_SHARED_LINKER_FLAGS="-Wl,-rpath-link,${PREFIX}/lib" \
    -DCMAKE_C_COMPILER="${PREFIX}/bin/aarch64-conda-linux-gnu-cc" \
    -DCMAKE_CXX_COMPILER="${PREFIX}/bin/aarch64-conda-linux-gnu-c++" \
    -DPARAVIEW_USE_MPI=ON -DPARAVIEW_USE_PYTHON=ON -DPARAVIEW_USE_QT=OFF \
    -DPARAVIEW_USE_FORTRAN=OFF -DPARAVIEW_USE_VISKORES=OFF \
    -DPARAVIEW_ENABLE_WEB=OFF -DPARAVIEW_ENABLE_XDMF2=OFF \
    -DPARAVIEW_ENABLE_CGNS_READER=OFF -DPARAVIEW_ENABLE_CGNS_WRITER=OFF \
    -DBUILD_TESTING=OFF -DPARAVIEW_BUILD_TESTING=OFF -DPARAVIEW_BUILD_VTK_TESTING=OFF \
    -DVTK_USE_X=OFF -DVTK_USE_Wayland=OFF -DVTK_USE_WAYLAND_OPENGL=OFF \
    -DVTK_OPENGL_HAS_EGL=ON \
    -DPython3_EXECUTABLE="${PREFIX}/bin/python" -DPython3_ROOT_DIR="$PREFIX" \
    -DMPI_C_COMPILER="${PREFIX}/bin/mpicc" -DMPI_CXX_COMPILER="${PREFIX}/bin/mpicxx"
cmake --build "${BUILD_ROOT}/build" --parallel "${PARAVIEW_BUILD_JOBS:-32}"
cmake --install "${BUILD_ROOT}/build"
cp "${BUILD_ROOT}/build/CMakeCache.txt" "${APP_DIR}/build-config.cmake.txt"
env -u LD_LIBRARY_PATH -u PYTHONPATH -u PYTHONHOME "${PREFIX}/bin/pvserver" --version
