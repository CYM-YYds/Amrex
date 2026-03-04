#!/bin/bash
# utils.sh
# ------------------------------------------------------------
# 通用工具函数：
#   - parse_args          解析 compile.sh 的命令行参数
#   - info                统一时间戳日志输出
#   - find_project_root   从起始目录向上查找算例根目录
#
# 约定：
#   - 本模块不做任何副作用操作（不 cd、不执行 make）。
#   - 只负责“字符串/路径/参数”这类轻量逻辑。

parse_args() {
	# 目前只解析提交相关参数，其余参数透传保留，便于未来扩展。
	local rest=()
	while [[ $# -gt 0 ]]; do
		case "$1" in
		--submit | --submit-after)
			SUBMIT_AFTER=1
			;;
		--no-submit)
			SUBMIT_AFTER=0
			;;
		--)
			shift
			rest+=("$@")
			break
			;;
		*)
			rest+=("$1")
			;;
		esac
		shift || break
	done
	SCRIPT_ARGS=("${rest[@]}")
}

info() { echo "[$(date '+%F %T')] $*"; }

find_project_root() {
	# 判断标准：同时包含 GNUmakefile 且具备 src/Make.package/config 之一。
	# 这样可兼容当前项目里“包装 GNUmakefile + config/GNUmakefile”布局。
	local dir="$1"
	while [[ -n "$dir" && "$dir" != "/" ]]; do
		if [[ -f "$dir/GNUmakefile" ]]; then
			if [[ -d "$dir/src" || -f "$dir/Make.package" || -d "$dir/config" ]]; then
				printf '%s\n' "$dir"
				return 0
			fi
		fi
		dir="$(dirname "$dir")"
	done
	return 1
}
