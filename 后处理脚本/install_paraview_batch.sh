#!/usr/bin/env bash
# 独立安装 ARM64 ParaView 应用，不替换系统 ParaView，也不更改用户启动配置。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
APP_DIR="${REPO_DIR}/.tools/paraview-batch"
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
LOCK="${SCRIPT_DIR}/paraview-batch-linux-aarch64.lock"
# 以包管理器已有元数据为安装状态来源；锁文件匹配时不重复重链整个环境。
if [[ -x "${APP_DIR}/env/bin/pvbatch" && -f "${LOCK}" ]] && \
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
    echo 'Pinned ParaView environment already installed; skipping package relink.'
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
            'paraview=6.2.0' 'python=3.12' -y
    fi
fi
env -u LD_LIBRARY_PATH -u PYTHONPATH -u PYTHONHOME "${APP_DIR}/env/bin/pvbatch" --version
