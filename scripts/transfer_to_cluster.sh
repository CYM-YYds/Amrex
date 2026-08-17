#!/usr/bin/env bash
set -Eeuo pipefail

# 使用 rclone 的 SFTP 后端把本仓库复制到另一台超算。
# 首次使用请填写下方配置并保持 DRY_RUN=1；确认输出无误后再改为 0。

# ==================== 用户配置区 ====================
REMOTE_HOST="example.cluster.edu"      # 目标集群登录节点或数据传输节点
REMOTE_USER="your_username"            # 目标集群用户名
REMOTE_PORT="22"                       # SSH/SFTP 端口
REMOTE_DIR="/path/on/remote/Amrex"     # 目标目录；绝对路径以 / 开头
SSH_KEY_FILE="${HOME}/.ssh/id_ed25519" # 私钥路径；不要填写公钥 .pub 文件

# 默认传输脚本所在仓库的根目录。也可以改为其他绝对路径。
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
SOURCE_DIR=$(cd -- "${SCRIPT_DIR}/.." && pwd -P)

DRY_RUN=1           # 1：仅预演；0：实际传输并在完成后校验
TRANSFERS=4         # 并行文件传输数；超算有限制时请调低
CHECKERS=8          # 并行检查数
LOG_FILE="${SCRIPT_DIR}/rclone-transfer.log"

# 如不需要传输某些目录，可在此添加规则。默认完整传输，不排除任何内容。
# 示例：EXCLUDES=(".git/**" "**/tmp_build_dir/**")
EXCLUDES=()
# ==================== 用户配置结束 ====================

die() {
	printf '错误: %s\n' "$*" >&2
	exit 1
}

command -v rclone >/dev/null 2>&1 || die "未找到 rclone，请先在当前集群安装或加载 rclone。"
[[ -d "$SOURCE_DIR" ]] || die "源目录不存在: $SOURCE_DIR"
[[ -r "$SSH_KEY_FILE" ]] || die "SSH 私钥不存在或不可读: $SSH_KEY_FILE"
[[ "$REMOTE_HOST" != "example.cluster.edu" ]] || die "请先填写 REMOTE_HOST。"
[[ "$REMOTE_USER" != "your_username" ]] || die "请先填写 REMOTE_USER。"
[[ "$REMOTE_DIR" != "/path/on/remote/Amrex" ]] || die "请先填写 REMOTE_DIR。"
[[ "$REMOTE_PORT" =~ ^[0-9]+$ ]] || die "REMOTE_PORT 必须是数字。"
[[ "$DRY_RUN" == "0" || "$DRY_RUN" == "1" ]] || die "DRY_RUN 只能设为 0 或 1。"

# 使用临时的 on-the-fly SFTP remote，不会修改 ~/.config/rclone/rclone.conf。
export RCLONE_SFTP_HOST="$REMOTE_HOST"
export RCLONE_SFTP_USER="$REMOTE_USER"
export RCLONE_SFTP_PORT="$REMOTE_PORT"
export RCLONE_SFTP_KEY_FILE="$SSH_KEY_FILE"

REMOTE=":sftp:${REMOTE_DIR}"
COMMON_ARGS=(
	--progress
	--transfers "$TRANSFERS"
	--checkers "$CHECKERS"
	--retries 10
	--low-level-retries 20
	--create-empty-src-dirs
	--log-file "$LOG_FILE"
	--log-level INFO
)

FILTER_ARGS=()
for pattern in "${EXCLUDES[@]}"; do
	FILTER_ARGS+=(--exclude "$pattern")
done

printf '源目录: %s\n' "$SOURCE_DIR"
printf '目标目录: %s@%s:%s\n' "$REMOTE_USER" "$REMOTE_HOST" "$REMOTE_DIR"
printf '日志文件: %s\n' "$LOG_FILE"

printf '\n[1/3] 检查 SFTP 连接……\n'
rclone lsd ":sftp:" --max-depth 1 >/dev/null

if [[ "$DRY_RUN" == "1" ]]; then
	printf '\n[2/3] 执行预演，不会写入目标集群……\n'
	rclone copy "${SOURCE_DIR}/" "$REMOTE" "${COMMON_ARGS[@]}" "${FILTER_ARGS[@]}" --dry-run
	printf '\n预演完成。确认列表和目标路径无误后，将 DRY_RUN 改为 0 再执行。\n'
	exit 0
fi

printf '\n[2/3] 开始传输……\n'
# copy 不会删除目标端已有的额外文件，比 sync 更适合首次迁移。
rclone copy "${SOURCE_DIR}/" "$REMOTE" "${COMMON_ARGS[@]}" "${FILTER_ARGS[@]}"

printf '\n[3/3] 校验源端与目标端文件……\n'
rclone check "${SOURCE_DIR}/" "$REMOTE" \
	--one-way \
	--checkers "$CHECKERS" \
	--retries 10 \
	--low-level-retries 20 \
	--log-file "$LOG_FILE" \
	--log-level INFO \
	"${FILTER_ARGS[@]}"

printf '\n传输和校验均已完成。详细日志: %s\n' "$LOG_FILE"
