#!/usr/bin/env bash
set -Eeuo pipefail

# 复制开发文件；历史结果留在原算例，已有目标目录一律拒绝覆盖。
usage() {
    echo "用法: $0 [--dry-run] SOURCE_DIR DEST_DIR"
    echo "复制 Git 跟踪及未忽略的文件，排除 runs/logs/data/tmp、checkpoint、构建产物和编译数据库。"
}
DRY_RUN=0
if [[ ${1:-} == --dry-run ]]; then
    DRY_RUN=1
    shift
fi
if [[ ${1:-} == --help || ${1:-} == -h ]]; then
    usage
    exit 0
fi
if [[ $# != 2 ]]; then
    usage >&2
    exit 1
fi
for tool in git rsync realpath python3; do
    command -v "$tool" >/dev/null || { echo "错误: 未找到 $tool" >&2; exit 1; }
done
SOURCE_DIR=$(realpath -e -- "$1")
DEST_DIR=$(realpath -m -- "$2")
[[ -d "$SOURCE_DIR" ]] || { echo "错误: 源目录不存在" >&2; exit 1; }
[[ ! -e "$DEST_DIR" && ! -L "$DEST_DIR" ]] || { echo "错误: 目标已存在: $DEST_DIR" >&2; exit 1; }
[[ "$DEST_DIR" != "$SOURCE_DIR/"* && "$DEST_DIR" != *[[:space:]]* ]] || {
    echo "错误: 目标不能位于源目录内，且路径不能包含空白" >&2; exit 1;
}
git -C "$SOURCE_DIR" rev-parse --show-toplevel >/dev/null
# 必须使用算例目录，避免误把整个仓库当作源目录。
[[ -d "$SOURCE_DIR/src" && -f "$SOURCE_DIR/config/GNUmakefile" ]] || {
    echo "错误: 源目录不是具有 src 和 config/GNUmakefile 的算例" >&2; exit 1;
}
tmp_list=$(mktemp)
trap 'rm -f "$tmp_list"' EXIT
# Git 清单保留反选规则；额外排除实验历史，即使这些文件已被跟踪。
git -C "$SOURCE_DIR" ls-files -z --cached --others --exclude-standard -- . > "$tmp_list"
python3 - "$SOURCE_DIR" "$tmp_list" <<'PY'
import os
import pathlib
import sys
source, listing = sys.argv[1:]
excluded = {'runs', 'logs', 'data', 'tmp', 'tmp_build_dir', '__pycache__', '.cache', '.git'}
paths = pathlib.Path(listing).read_bytes().split(b'\0')
kept = []
for raw in paths:
    if not raw:
        continue
    name = os.fsdecode(raw)
    path = pathlib.PurePosixPath(name)
    if any(p in excluded or p.startswith(('chk', 'plt', 'data32to')) for p in path.parts[:-1]):
        continue
    if path.name == 'compile_commands.json' or path.suffix in {'.ex', '.o', '.cuo', '.log', '.out', '.err', '.pyc'}:
        continue
    if os.path.lexists(os.path.join(source, name)):
        kept.append(raw)
pathlib.Path(listing).write_bytes(b''.join(p + b'\0' for p in dict.fromkeys(kept)))
print(f'开发文件数量: {len(set(kept))}')
PY
[[ -s "$tmp_list" ]] || { echo "错误: 文件清单为空" >&2; exit 1; }
echo "源: $SOURCE_DIR"
echo "目标: $DEST_DIR"
if (( DRY_RUN )); then
    # 预览不创建目标或其父目录。
    while IFS= read -r -d '' file; do printf '%s\n' "$file"; done < "$tmp_list"
    exit 0
fi
mkdir -p -- "$(dirname -- "$DEST_DIR")"
mkdir -- "$DEST_DIR"
rsync -a --from0 --files-from="$tmp_list" "$SOURCE_DIR/" "$DEST_DIR/"
# 保留源算例的 AMReX 版本和全部编辑器选项，只替换算例绝对路径。
if [[ -f "$DEST_DIR/.clangd" ]]; then
    python3 - "$DEST_DIR/.clangd" "$SOURCE_DIR" "$DEST_DIR" <<'PY'
import pathlib
import re
import sys
file, source, dest = sys.argv[1:]
path = pathlib.Path(file)
text = path.read_text()
text = text.replace(re.escape(source), re.escape(dest)).replace(source, dest)
path.write_text(text)
PY
fi
echo "完成: $DEST_DIR"
