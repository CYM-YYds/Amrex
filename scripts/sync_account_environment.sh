#!/usr/bin/env bash
set -Eeuo pipefail

# 在同一集群的两个账号之间，通过本仓库共享目录迁移 Codex 和 VS Code 配置。
#
# 旧账号：填写 TARGET_USER 后执行
#   ./scripts/sync_account_environment.sh export
# 新账号：执行
#   ./scripts/sync_account_environment.sh import
#
# 安全约定：不迁移 Codex auth.json、.env、缓存、socket、锁文件及 VS Code 认证存储。

# ==================== 用户配置区 ====================
TARGET_USER="Liuzhaohui" # export 时必填：新账号的系统用户名

# 默认以环境变量 CODEX_HOME 为准，未设置时使用 ~/.codex。
SOURCE_CODEX_HOME="${SOURCE_CODEX_HOME:-${CODEX_HOME:-${HOME}/.codex}}"
DEST_CODEX_HOME="${DEST_CODEX_HOME:-${CODEX_HOME:-${HOME}/.codex}}"

SOURCE_VSCODE_HOME="${SOURCE_VSCODE_HOME:-${HOME}/.vscode-server}"
DEST_VSCODE_HOME="${DEST_VSCODE_HOME:-${HOME}/.vscode-server}"

INCLUDE_CODEX_SESSIONS="${INCLUDE_CODEX_SESSIONS:-1}"       # 1：迁移会话和历史
INCLUDE_VSCODE_EXTENSIONS="${INCLUDE_VSCODE_EXTENSIONS:-1}" # 1：复制远端扩展
# ==================== 用户配置结束 ====================

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
REPO_ROOT=$(cd -- "${SCRIPT_DIR}/.." && pwd -P)
TRANSFER_DIR="${TRANSFER_DIR:-${REPO_ROOT}/.account-migration}"
LATEST_FILE="${TRANSFER_DIR}/LATEST"
WORK_TEMP_DIR=""

cleanup() {
	if [[ -n "$WORK_TEMP_DIR" && -d "$WORK_TEMP_DIR" ]]; then
		rm -rf -- "$WORK_TEMP_DIR"
	fi
}
trap cleanup EXIT

die() {
	printf '错误: %s\n' "$*" >&2
	exit 1
}

usage() {
	cat <<'EOF'
用法：
  sync_account_environment.sh export
  sync_account_environment.sh import [迁移包路径]
  sync_account_environment.sh inspect [迁移包路径]

export  由旧账号执行，创建迁移包并授权给 TARGET_USER。
import  由新账号执行，校验迁移包、备份现有配置后导入。
inspect 查看迁移包内容，不修改任何配置。
EOF
}

copy_if_exists() {
	local source=$1 destination_dir=$2
	if [[ -e "$source" || -L "$source" ]]; then
		mkdir -p "$destination_dir"
		cp -a -- "$source" "$destination_dir/"
	fi
}

resolve_archive() {
	local requested=${1:-} archive_name
	if [[ -n "$requested" ]]; then
		[[ -f "$requested" ]] || die "迁移包不存在: $requested"
		printf '%s\n' "$requested"
		return 0
	fi
	[[ -r "$LATEST_FILE" ]] || die "找不到 $LATEST_FILE；可显式提供迁移包路径。"
	IFS= read -r archive_name <"$LATEST_FILE"
	[[ "$archive_name" != */* ]] || die "LATEST 内容不安全: $archive_name"
	[[ -f "${TRANSFER_DIR}/${archive_name}" ]] || die "LATEST 指向的迁移包不存在。"
	printf '%s\n' "${TRANSFER_DIR}/${archive_name}"
}

verify_archive() {
	local archive=$1 checksum_file="${archive}.sha256"
	[[ -r "$checksum_file" ]] || die "缺少校验文件: $checksum_file"
	(
		cd -- "$(dirname -- "$archive")"
		sha256sum --check -- "$(basename -- "$checksum_file")"
	)
}

export_environment() {
	local timestamp archive codex_stage vscode_stage
	[[ -n "$TARGET_USER" ]] || die "请先在脚本顶部填写 TARGET_USER。"
	id "$TARGET_USER" >/dev/null 2>&1 || die "目标账号不存在: $TARGET_USER"
	[[ -d "$SOURCE_CODEX_HOME" ]] || die "Codex 目录不存在: $SOURCE_CODEX_HOME"
	[[ "$INCLUDE_CODEX_SESSIONS" == "0" || "$INCLUDE_CODEX_SESSIONS" == "1" ]] || \
		die "INCLUDE_CODEX_SESSIONS 只能设为 0 或 1。"
	[[ "$INCLUDE_VSCODE_EXTENSIONS" == "0" || "$INCLUDE_VSCODE_EXTENSIONS" == "1" ]] || \
		die "INCLUDE_VSCODE_EXTENSIONS 只能设为 0 或 1。"
	command -v sha256sum >/dev/null 2>&1 || die "未找到 sha256sum。"
	command -v setfacl >/dev/null 2>&1 || die "未找到 setfacl，无法安全授权给目标账号。"

	timestamp=$(date '+%Y%m%d-%H%M%S')
	archive="${TRANSFER_DIR}/account-environment-${timestamp}.tar.gz"
	WORK_TEMP_DIR=$(mktemp -d)
	codex_stage="${WORK_TEMP_DIR}/codex"
	vscode_stage="${WORK_TEMP_DIR}/vscode-server"
	mkdir -p "$codex_stage" "$vscode_stage"

	printf '[1/5] 收集 Codex 配置……\n'
	for item in config.toml AGENTS.md rules skills plugins memories; do
		copy_if_exists "${SOURCE_CODEX_HOME}/${item}" "$codex_stage"
	done
	if [[ "$INCLUDE_CODEX_SESSIONS" == "1" ]]; then
		for item in sessions archived_sessions history.jsonl session_index.jsonl attachments; do
			copy_if_exists "${SOURCE_CODEX_HOME}/${item}" "$codex_stage"
		done
	fi

	printf '[2/5] 收集 VS Code 远端配置……\n'
	copy_if_exists "${SOURCE_VSCODE_HOME}/data/Machine/settings.json" \
		"${vscode_stage}/data/Machine"
	if [[ "$INCLUDE_VSCODE_EXTENSIONS" == "1" ]]; then
		copy_if_exists "${SOURCE_VSCODE_HOME}/extensions" "$vscode_stage"
	fi

	cat >"${WORK_TEMP_DIR}/MIGRATION_INFO.txt" <<EOF
exported_at=${timestamp}
source_user=$(id -un 2>/dev/null || id -u)
source_host=$(hostname)
source_codex_home=${SOURCE_CODEX_HOME}
source_vscode_home=${SOURCE_VSCODE_HOME}
includes_codex_sessions=${INCLUDE_CODEX_SESSIONS}
includes_vscode_extensions=${INCLUDE_VSCODE_EXTENSIONS}
excluded_secrets=auth.json,.env,VS_Code_globalStorage
EOF

	printf '[3/5] 创建迁移包……\n'
	mkdir -p "$TRANSFER_DIR"
	tar -czf "$archive" -C "$WORK_TEMP_DIR" .
	(
		cd -- "$TRANSFER_DIR"
		sha256sum -- "$(basename -- "$archive")" >"$(basename -- "$archive").sha256"
	)
	printf '%s\n' "$(basename -- "$archive")" >"$LATEST_FILE"

	printf '[4/5] 设置目标账号 ACL……\n'
	chmod 700 "$TRANSFER_DIR"
	chmod 600 "$archive" "${archive}.sha256" "$LATEST_FILE"
	setfacl -m "u:${TARGET_USER}:--x" "$TRANSFER_DIR"
	setfacl -m "u:${TARGET_USER}:r--" "$archive" "${archive}.sha256" "$LATEST_FILE"

	printf '[5/5] 验证迁移包……\n'
	verify_archive "$archive"
	printf '\n导出完成：%s\n' "$archive"
	printf '请切换到账号 %s 后执行：\n' "$TARGET_USER"
	printf '  %q import %q\n' "$0" "$archive"
}

backup_destination() {
	local backup_root=$1
	mkdir -p "${backup_root}/codex" "${backup_root}/vscode-server/data/Machine"
	for item in config.toml AGENTS.md rules skills plugins memories sessions archived_sessions \
		history.jsonl session_index.jsonl attachments; do
		copy_if_exists "${DEST_CODEX_HOME}/${item}" "${backup_root}/codex"
	done
	copy_if_exists "${DEST_VSCODE_HOME}/data/Machine/settings.json" \
		"${backup_root}/vscode-server/data/Machine"
	copy_if_exists "${DEST_VSCODE_HOME}/extensions" "${backup_root}/vscode-server"
}

import_environment() {
	local archive=$1 backup_root timestamp
	command -v sha256sum >/dev/null 2>&1 || die "未找到 sha256sum。"
	verify_archive "$archive"

	timestamp=$(date '+%Y%m%d-%H%M%S')
	backup_root="${HOME}/.account-migration-backups/${timestamp}"
	printf '[1/4] 备份新账号的现有配置到 %s……\n' "$backup_root"
	backup_destination "$backup_root"

	WORK_TEMP_DIR=$(mktemp -d)
	printf '[2/4] 解压并检查迁移包……\n'
	tar -xzf "$archive" --no-same-owner --no-same-permissions -C "$WORK_TEMP_DIR"
	[[ -d "${WORK_TEMP_DIR}/codex" ]] || die "迁移包缺少 codex 目录。"

	printf '[3/4] 导入 Codex 配置……\n'
	mkdir -p "$DEST_CODEX_HOME"
	cp -a -- "${WORK_TEMP_DIR}/codex/." "$DEST_CODEX_HOME/"

	printf '[4/4] 导入 VS Code 远端配置……\n'
	if [[ -d "${WORK_TEMP_DIR}/vscode-server" ]]; then
		mkdir -p "$DEST_VSCODE_HOME"
		cp -a -- "${WORK_TEMP_DIR}/vscode-server/." "$DEST_VSCODE_HOME/"
	fi

	chmod 700 "$DEST_CODEX_HOME" 2>/dev/null || true
	printf '\n导入完成。原配置备份位于：%s\n' "$backup_root"
	printf 'auth.json 和 VS Code 登录状态未迁移；请在新账号中重新登录。\n'
}

main() {
	local action=${1:-} archive
	case "$action" in
		export)
			export_environment
			;;
		import)
			archive=$(resolve_archive "${2:-}")
			import_environment "$archive"
			;;
		inspect)
			archive=$(resolve_archive "${2:-}")
			verify_archive "$archive"
			tar -tzf "$archive"
			;;
		-h | --help | help | '')
			usage
			;;
		*)
			usage >&2
			die "未知操作: $action"
			;;
	esac
}

main "$@"
