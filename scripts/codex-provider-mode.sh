#!/usr/bin/env bash
set -Eeuo pipefail

# 在 Codex 官方账号模式（openai）与第三方 API 模式（custom）之间切换，
# 并通过 codex-provider 备份和同步会话元数据。

die() {
	printf '错误: %s\n' "$*" >&2
	exit 1
}

usage() {
	cat <<'EOF'
用法：
  ./scripts/codex-provider-mode.sh [openai|custom]

不带参数时自动切换到另一种模式。切换成功后，在 VS Code 命令面板运行
“Developer: Reload Window”以重新加载 Codex 扩展。
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
	usage
	exit 0
fi

[[ $# -le 1 ]] || die "最多接受一个目标 provider 参数。"

command -v codex-provider >/dev/null 2>&1 || die "未找到 codex-provider；请先安装并确认它在 PATH 中。"

case "${1:-}" in
	""|toggle)
		if ! status_output=$(codex-provider status 2>&1); then
			printf '%s\n' "$status_output" >&2
			die "无法读取当前 Codex provider 状态。"
		fi
		current_provider=$(sed -n 's/^Current provider: \([^[:space:]]*\).*/\1/p' <<<"$status_output")
		case "$current_provider" in
			custom) target_provider=openai ;;
			openai) target_provider=custom ;;
			*) die "当前 provider 为 '${current_provider:-未知}'；请显式指定 openai 或 custom。" ;;
		esac
		;;
	openai|custom)
		target_provider=$1
		;;
	*)
		usage >&2
		die "只支持 openai、custom 或 toggle。"
		;;
esac

printf '切换 Codex provider：%s\n' "$target_provider"
codex-provider switch "$target_provider"
printf '\n已完成配置切换和会话同步。请在 VS Code 命令面板执行：\n'
printf '  Developer: Reload Window\n'
