#!/usr/bin/env bash
set -Eeuo pipefail

# 在同一集群的两个账号之间，通过本仓库共享目录迁移 ~/.nvm。
# 不迁移 ~/.npm：它主要是可重新生成的下载缓存。
#
# 旧账号：填写 TARGET_USER 后执行
#   ./scripts/sync_nvm_environment.sh export
# 新账号：执行
#   ./scripts/sync_nvm_environment.sh import

# ==================== 用户配置区 ====================
TARGET_USER="${TARGET_USER:-Liuzhaohui}" # 可通过环境变量覆盖
SOURCE_NVM_DIR="${SOURCE_NVM_DIR:-${HOME}/.nvm}"
DEST_NVM_DIR="${DEST_NVM_DIR:-${HOME}/.nvm}"
EXCLUDE_NVM_DOWNLOAD_CACHE="${EXCLUDE_NVM_DOWNLOAD_CACHE:-1}"
# ==================== 用户配置结束 ====================

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
REPO_ROOT=$(cd -- "${SCRIPT_DIR}/.." && pwd -P)
TRANSFER_DIR="${TRANSFER_DIR:-${REPO_ROOT}/.account-migration}"
LATEST_FILE="${TRANSFER_DIR}/LATEST_NVM"
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
  sync_nvm_environment.sh export
  sync_nvm_environment.sh inspect [迁移包路径]
  sync_nvm_environment.sh import [迁移包路径]

export  由旧账号执行，打包 ~/.nvm 并授权给 TARGET_USER。
inspect 校验并查看迁移包，不修改任何文件。
import  由新账号执行，备份已有 ~/.nvm 后导入并验证 Node/npm。
EOF
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
	[[ -n "$archive_name" && "$archive_name" != */* ]] || die "LATEST_NVM 内容不安全。"
	[[ -f "${TRANSFER_DIR}/${archive_name}" ]] || die "LATEST_NVM 指向的迁移包不存在。"
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

export_nvm() {
	local timestamp archive source_parent source_name source_user
	[[ -n "$TARGET_USER" ]] || die "请先在脚本顶部填写 TARGET_USER。"
	id "$TARGET_USER" >/dev/null 2>&1 || die "目标账号不存在: $TARGET_USER"
	[[ -d "$SOURCE_NVM_DIR" ]] || die "NVM 目录不存在: $SOURCE_NVM_DIR"
	[[ -r "${SOURCE_NVM_DIR}/nvm.sh" ]] || die "未找到 ${SOURCE_NVM_DIR}/nvm.sh"
	[[ "$EXCLUDE_NVM_DOWNLOAD_CACHE" == "0" || "$EXCLUDE_NVM_DOWNLOAD_CACHE" == "1" ]] || \
		die "EXCLUDE_NVM_DOWNLOAD_CACHE 只能设为 0 或 1。"
	command -v sha256sum >/dev/null 2>&1 || die "未找到 sha256sum。"
	command -v setfacl >/dev/null 2>&1 || die "未找到 setfacl，无法安全授权给目标账号。"

	timestamp=$(date '+%Y%m%d-%H%M%S')
	archive="${TRANSFER_DIR}/nvm-environment-${timestamp}.tar.gz"
	source_parent=$(cd -- "$(dirname -- "$SOURCE_NVM_DIR")" && pwd -P)
	source_name=$(basename -- "$SOURCE_NVM_DIR")
	source_user=$(id -un 2>/dev/null || id -u)
	mkdir -p "$TRANSFER_DIR"

	printf '[1/4] 打包 NVM 环境：%s……\n' "$SOURCE_NVM_DIR"
	if [[ "$EXCLUDE_NVM_DOWNLOAD_CACHE" == "1" ]]; then
		tar -czf "$archive" \
			--exclude="${source_name}/.cache" \
			-C "$source_parent" "$source_name"
	else
		tar -czf "$archive" -C "$source_parent" "$source_name"
	fi

	printf '[2/4] 生成 SHA-256 校验文件……\n'
	(
		cd -- "$TRANSFER_DIR"
		sha256sum -- "$(basename -- "$archive")" >"$(basename -- "$archive").sha256"
	)
	printf '%s\n' "$(basename -- "$archive")" >"$LATEST_FILE"

	printf '[3/4] 设置目标账号 ACL……\n'
	chmod 700 "$TRANSFER_DIR"
	chmod 600 "$archive" "${archive}.sha256" "$LATEST_FILE"
	setfacl -m "u:${TARGET_USER}:--x" "$TRANSFER_DIR"
	setfacl -m "u:${TARGET_USER}:r--" "$archive" "${archive}.sha256" "$LATEST_FILE"

	printf '[4/4] 验证迁移包……\n'
	verify_archive "$archive"
	printf '\n导出完成：%s\n' "$archive"
	printf '来源账号：%s\n' "$source_user"
	printf '请切换到账号 %s 后执行：\n' "$TARGET_USER"
	printf '  %q import %q\n' "$0" "$archive"
}

validate_archive_layout() {
	local archive=$1 first_component invalid_entry
	first_component=$(tar -tzf "$archive" | sed -n '1{s#^\./##; s#/.*##; p;}')
	[[ -n "$first_component" ]] || die "迁移包为空。"
	invalid_entry=$(tar -tzf "$archive" | awk '
		/^\// { print; exit }
		/(^|\/)\.\.($|\/)/ { print; exit }
	' || true)
	[[ -z "$invalid_entry" ]] || die "迁移包包含不安全路径: $invalid_entry"
	printf '%s\n' "$first_component"
}

verify_installed_nvm() {
	local nvm_dir=$1
	(
		set +u
		export NVM_DIR="$nvm_dir"
		# shellcheck source=/dev/null
		. "${NVM_DIR}/nvm.sh"
		nvm use default >/dev/null
		printf 'Node: %s (%s)\n' "$(node --version)" "$(command -v node)"
		printf 'npm:  %s (%s)\n' "$(npm --version)" "$(command -v npm)"
	)
}

import_nvm() {
	local archive=$1 archive_root timestamp backup_dir extracted_dir
	command -v sha256sum >/dev/null 2>&1 || die "未找到 sha256sum。"
	verify_archive "$archive"
	archive_root=$(validate_archive_layout "$archive")

	WORK_TEMP_DIR=$(mktemp -d "${HOME}/.nvm-import.XXXXXX")
	printf '[1/4] 解压并检查迁移包……\n'
	tar -xzf "$archive" --no-same-owner --no-same-permissions -C "$WORK_TEMP_DIR"
	extracted_dir="${WORK_TEMP_DIR}/${archive_root}"
	[[ -r "${extracted_dir}/nvm.sh" ]] || die "迁移包中未找到 nvm.sh。"

	printf '[2/4] 备份新账号已有的 NVM……\n'
	if [[ -e "$DEST_NVM_DIR" || -L "$DEST_NVM_DIR" ]]; then
		timestamp=$(date '+%Y%m%d-%H%M%S')
		backup_dir="${DEST_NVM_DIR}.before-account-migration-${timestamp}"
		mv -- "$DEST_NVM_DIR" "$backup_dir"
		printf '旧 NVM 已移动到：%s\n' "$backup_dir"
	fi

	printf '[3/4] 安装迁移的 NVM……\n'
	mkdir -p "$(dirname -- "$DEST_NVM_DIR")"
	mv -- "$extracted_dir" "$DEST_NVM_DIR"

	printf '[4/4] 验证默认 Node/npm……\n'
	verify_installed_nvm "$DEST_NVM_DIR"

	cat <<EOF

迁移完成。请确认新账号的 ~/.bashrc 包含：

export NVM_DIR="\$HOME/.nvm"
[ -s "\$NVM_DIR/nvm.sh" ] && . "\$NVM_DIR/nvm.sh"
[ -s "\$NVM_DIR/bash_completion" ] && . "\$NVM_DIR/bash_completion"

重新登录终端后，可执行：node --version && npm --version
EOF
}

main() {
	local action=${1:-} archive
	case "$action" in
		export)
			export_nvm
			;;
		inspect)
			archive=$(resolve_archive "${2:-}")
			verify_archive "$archive"
			validate_archive_layout "$archive" >/dev/null
			tar -tzf "$archive"
			;;
		import)
			archive=$(resolve_archive "${2:-}")
			import_nvm "$archive"
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
