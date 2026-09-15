#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
CASE_NAME="${1:-}"

if [[ ! "${CASE_NAME}" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]]; then
    echo "用法: $0 <case-name>" >&2
    echo "case-name 只能包含字母、数字、点、下划线和连字符。" >&2
    exit 2
fi

TARGET_DIR="${CASE_DIR}/cases/${CASE_NAME}"
TARGET_INPUTS="${TARGET_DIR}/inputs"
if [[ -e "${TARGET_DIR}" ]]; then
    echo "ERROR: 算例目录已存在: ${TARGET_DIR}" >&2
    exit 1
fi

# 新算例从当前维护的 Re3200 OSI 定义复制，创建后再按需修改参数。
mkdir -p "${TARGET_DIR}"
cp "${CASE_DIR}/cases/re3200_osi/inputs" "${TARGET_INPUTS}"
echo "已创建: ${TARGET_INPUTS}"
