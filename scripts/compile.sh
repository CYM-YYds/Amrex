#!/bin/bash
# compile.sh（主入口）
# ------------------------------------------------------------
# 作用：
#   1) 解析参数与环境变量（并行度、CUDA 架构、CCDB 开关等）
#   2) 定位项目根目录并初始化日志
#   3) 加载模块化功能并转交给构建入口 run_build_entry
#
# 设计原则：
#   - 本文件只做“编排”，尽量不放业务细节。
#   - 具体实现放在 scripts/lib/*.sh，便于维护和复用。
#
# 模块分工：
#   - lib/utils.sh   : 通用工具（参数解析、日志打印、项目根目录发现）
#   - lib/env.sh     : 环境加载与 AMReX 版本字符串推断
#   - lib/logging.sh : 日志重定向、轮转、摘要生成
#   - lib/ccdb.sh    : 构建执行 + compile_commands.json 生成
#   - lib/build.sh   : 本地/远程编译调度与提交后处理
#
# 维护提示：
#   - 若修改编译行为，优先改 lib/ccdb.sh 或 lib/build.sh。
#   - 若修改 CLI 参数或默认值，优先改本文件顶部配置区与 lib/utils.sh。
#   - 若仅需观察构建结果，查看 logs/compile-latest.log 与 *-summary.log。
#
# 使用说明（环境变量，可选）：
#   - MAKE_J   : make 并行度（默认 8）
#       例：MAKE_J=16 ./compile.sh
#   - CUDA_ARCH: 传递给 Make 的 CUDA 架构号（默认 80）
#       例：CUDA_ARCH=86 ./compile.sh
#       例：GEN_CCDB=0 ./compile.sh   # 跳过生成编译数据库，构建更快
#   - CCDB_METHOD: 已固定为 wrapper（保留该变量仅为兼容，不再切换其他模式）
#       例：CCDB_METHOD=wrapper GEN_CCDB=1 ./compile.sh
#   - CCDB_OUTPUT_DIR: compile_commands.json 输出目录（默认 config）
#       例：CCDB_OUTPUT_DIR=. ./compile.sh  # 输出到项目根目录
#
# 运行逻辑：
#   - 如果当前主机名不是 whshare-agent-1，则通过 SSH 在 whshare-agent-1 上执行本脚本；
#   - 如果已经在 whshare-agent-1 上，则直接加载环境并构建；
#   - 若启用编译数据库生成，固定使用 gcc/g++/nvcc 包装器采集命令生成 compile_commands.json。
set -euo pipefail

# 可配置项（通过环境变量覆盖）
MAKE_J="${MAKE_J:-16}"
CUDA_ARCH="${CUDA_ARCH:-80}"
#   - GEN_CCDB : 是否生成编译数据库 compile_commands.json（0 不生成；1 生成；默认 1）
GEN_CCDB="${GEN_CCDB:-1}"
# 生成编译数据库的方法控制（已固定为 wrapper，保留变量仅兼容）
CCDB_METHOD="wrapper"
# 是否在构建遇错时继续尽量编译后续目标（有助于捕获更多编译命令，生成更完整的 CCDB）
MAKE_KEEP_GOING="${MAKE_KEEP_GOING:-0}"

# 可选：编译成功后是否自动提交作业（默认 0 不提交）。
# 通过 --submit / --submit-after 触发；--no-submit 显式关闭。
SUBMIT_AFTER="${SUBMIT_AFTER:-0}"
SUBMIT_CMD="${SUBMIT_CMD:-dsub -s ./scripts/submit.sh}"
# 提交必须在登录节点执行（避免编译节点缺少 dsub）；默认跳转 whshare-ccs-cli-2，可通过 SUBMIT_HOST 覆盖
SUBMIT_HOST="${SUBMIT_HOST:-whshare-ccs-cli-2}"
# 为避免编译节点加载的 GCC/LD_LIBRARY_PATH 影响 ssh，强制用干净环境调用 ssh
SUBMIT_SSH="${SUBMIT_SSH:-/usr/bin/env -u LD_LIBRARY_PATH ssh}"

# 控制编译数据库“胖/瘦”的可选开关：
#  - CCDB_KEEP_TMP=1    保留 nvcc 产生的临时 tmpxft_/cudafe* 记录（默认 0，不保留）
#  - CCDB_DEDUP_BY_FILE=0 禁用按 file 去重（默认 1，启用去重）
#  - CCDB_OUTPUT_DIR    compile_commands.json 输出目录（默认 config）
#  - CCDB_REQUIRE_FULL=1 无采集记录时强制 make -B 全量重建（默认 0，优先复用已有数据库）
#  - CCDB_MERGE_OLD=0   禁用将本次增量记录与旧数据库合并（默认 1，启用）
CCDB_KEEP_TMP="${CCDB_KEEP_TMP:-0}"
CCDB_DEDUP_BY_FILE="${CCDB_DEDUP_BY_FILE:-1}"
CCDB_OUTPUT_DIR="${CCDB_OUTPUT_DIR:-config}"
CCDB_REQUIRE_FULL="${CCDB_REQUIRE_FULL:-0}"
CCDB_MERGE_OLD="${CCDB_MERGE_OLD:-1}"

# 模块目录（优先使用上游显式传入；否则按当前脚本位置推断）
if [[ -n "${_CF_SCRIPT_DIR:-}" ]]; then
	SCRIPT_DIR="${_CF_SCRIPT_DIR}"
else
	_cf_src="${BASH_SOURCE[0]:-$0}"
	SCRIPT_DIR="$(cd "$(dirname "${_cf_src}")" && pwd)"
fi

if [[ ! -f "${SCRIPT_DIR}/lib/utils.sh" ]]; then
	echo "错误: 未找到模块目录 ${SCRIPT_DIR}/lib（当前 SCRIPT_DIR=${SCRIPT_DIR}）。" >&2
	echo "提示: 远端 stdin 执行时请传入 _CF_SCRIPT_DIR。" >&2
	exit 1
fi
source "${SCRIPT_DIR}/lib/utils.sh"
source "${SCRIPT_DIR}/lib/env.sh"
source "${SCRIPT_DIR}/lib/logging.sh"
source "${SCRIPT_DIR}/lib/ccdb.sh"
source "${SCRIPT_DIR}/lib/build.sh"

# 解析命令行（当前支持 --submit/--no-submit）
parse_args "$@"
set -- "${SCRIPT_ARGS[@]:-}"

# 记录脚本真实路径（远端通过 stdin 执行时用于回传自身内容）
if [[ -n "${BASH_SOURCE[0]:-}" && -f "${BASH_SOURCE[0]}" ]]; then
	if command -v realpath >/dev/null 2>&1; then
		_CF_SCRIPT_PATH="$(realpath "${BASH_SOURCE[0]}")"
	else
		_CF_SCRIPT_PATH="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"
	fi
else
	_CF_SCRIPT_PATH=""
fi

#              g'h'g'h'g'g
if [[ -n "${_CHANNELFLOW_PROJECT_ROOT:-}" ]]; then
	PROJECT_ROOT="${_CHANNELFLOW_PROJECT_ROOT}"
else
	start_dir=""
	if [[ -n "$_CF_SCRIPT_PATH" ]]; then
		start_dir="$(dirname "$_CF_SCRIPT_PATH")"
	else
		start_dir="$(pwd)"
	fi
	if ! PROJECT_ROOT=$(find_project_root "$start_dir"); then
		if ! PROJECT_ROOT=$(find_project_root "$(pwd)"); then
			echo "错误: 未能定位 ChannelFlow 项目根目录 (起始路径: $start_dir)" >&2
			exit 1
		fi
	fi
	export _CHANNELFLOW_PROJECT_ROOT="$PROJECT_ROOT"
fi

PROJECT_ROOT="$(cd "$PROJECT_ROOT" && pwd -P)"
cd "$PROJECT_ROOT"

# 初始化日志系统（可能重定向 stdout/stderr）
setup_logging

SCRIPT_PAYLOAD="${_CF_SCRIPT_PATH:-$0}"

# 统一进入构建入口（具体在 lib/build.sh）
run_build_entry
