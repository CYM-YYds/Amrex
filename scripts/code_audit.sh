#!/usr/bin/env bash
# code_audit.sh
# ------------------------------------------------------------
# 作用：对算例 src/ 做只读静态审计，输出重复代码、复杂度和死代码候选。
#   1) jscpd   : 复制粘贴式重复片段
#   2) lizard  : 过长/圈复杂度过高的函数
#   3) cppcheck: 未调用函数（主机 + CUDA 两种宏配置取交集）及可疑写法
#
# 用法：
#   scripts/code_audit.sh --install                 # 首次安装工具到 .tools/code_audit
#   scripts/code_audit.sh <case>/src [选项]
#
# 选项：
#   -o DIR        输出目录（默认 /tmp/code_audit/<算例名>）
#   -a FILE       未调用函数白名单，每行一个函数名，# 开头为注释
#   -t PCT        重复率阈值，超过时返回码为 2（默认不检查）
#   --min-tokens N  jscpd 最小克隆 token 数（默认 70）
#   --ccn N       lizard 圈复杂度报警阈值（默认 25）
#   --length N    lizard 函数长度报警阈值（默认 150）
#
# 说明：
#   - 本脚本不修改源码；工具装在 .tools/code_audit（已被 .gitignore 忽略）。
#   - unusedFunction 只是候选，宏、函数指针或外部调用仍可能造成误报，删除前需人工确认。
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOL_DIR="$REPO_ROOT/.tools/code_audit"
VENV="$TOOL_DIR/venv"
JSCPD="$TOOL_DIR/node_modules/.bin/jscpd"

# 固定工具版本，保证不同时间的审计结果可比
JSCPD_VER="4.0.5"
LIZARD_VER="1.17.13"
CPPCHECK_WHEEL_VER="1.5.3"   # 对应 Cppcheck 2.22.0

usage() { sed -n '2,24p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

install_tools() {
    mkdir -p "$TOOL_DIR"
    command -v npm >/dev/null || { echo "错误：需要 npm（Node.js）安装 jscpd" >&2; exit 1; }
    echo "[install] jscpd@$JSCPD_VER"
    npm install --silent --no-fund --no-audit --prefix "$TOOL_DIR" "jscpd@$JSCPD_VER"
    echo "[install] python venv: lizard==$LIZARD_VER cppcheck==$CPPCHECK_WHEEL_VER"
    [ -x "$VENV/bin/python" ] || python3 -m venv "$VENV"
    # 旧版 pip 无法识别 manylinux_2_28 wheel，会回退到源码编译并失败
    "$VENV/bin/pip" install -q --upgrade "pip==24.0"
    "$VENV/bin/pip" install -q "lizard==$LIZARD_VER" "cppcheck==$CPPCHECK_WHEEL_VER"
    "$VENV/bin/cppcheck" --version
    echo "[install] 完成：$TOOL_DIR"
}

# ---------------- 参数解析 ----------------
SRC="" OUT="" ALLOW="" THRESHOLD="" MIN_TOKENS=70 CCN=25 LENGTH=150
while [ $# -gt 0 ]; do
    case "$1" in
        --install) install_tools; exit 0 ;;
        -h|--help) usage; exit 0 ;;
        -o) OUT="$2"; shift 2 ;;
        -a) ALLOW="$2"; shift 2 ;;
        -t) THRESHOLD="$2"; shift 2 ;;
        --min-tokens) MIN_TOKENS="$2"; shift 2 ;;
        --ccn) CCN="$2"; shift 2 ;;
        --length) LENGTH="$2"; shift 2 ;;
        -*) echo "未知选项：$1" >&2; usage; exit 1 ;;
        *) SRC="$1"; shift ;;
    esac
done

[ -n "$SRC" ] || { usage; exit 1; }
[ -d "$SRC" ] || { echo "错误：目录不存在：$SRC" >&2; exit 1; }
SRC="$(cd "$SRC" && pwd)"
for t in "$JSCPD" "$VENV/bin/lizard" "$VENV/bin/cppcheck"; do
    [ -x "$t" ] || { echo "错误：缺少 $t，请先运行 scripts/code_audit.sh --install" >&2; exit 1; }
done
[ -z "$ALLOW" ] || [ -f "$ALLOW" ] || { echo "错误：白名单不存在：$ALLOW" >&2; exit 1; }

CASE_NAME="$(basename "$(dirname "$SRC")")"
OUT="${OUT:-/tmp/code_audit/$CASE_NAME}"
mkdir -p "$OUT"
# 维度从算例 GNUmakefile 的 DIM 读取，缺省为 3
DIM="$(sed -n 's/^[[:space:]]*DIM[[:space:]]*=[[:space:]]*\([0-9]\).*/\1/p' "$SRC/../config/GNUmakefile" 2>/dev/null | head -1)"
DIM="${DIM:-3}"
case "$DIM" in
    2) DDECL="a,b" ;;
    *) DDECL="a,b,c" ;;
esac

echo "[audit] 源码目录：$SRC"
echo "[audit] 输出目录：$OUT  (DIM=$DIM)"

# ---------------- 1) jscpd 重复代码 ----------------
# jscpd 默认不识别 .H，且会跳过 >1000 行或 >100KB 的文件，这里显式放开
echo "[1/3] jscpd"
"$JSCPD" --silent --min-tokens "$MIN_TOKENS" --min-lines 8 \
    --max-size 5mb --max-lines 100000 \
    --formats-exts "cpp:cpp,cxx,cc,c,H,h,hpp" \
    --reporters json --output "$OUT/jscpd" "$SRC" >/dev/null

# ---------------- 2) lizard 复杂度 ----------------
echo "[2/3] lizard"
"$VENV/bin/lizard" -l cpp --csv "$SRC" > "$OUT/lizard.csv" 2>/dev/null || true

# ---------------- 3) cppcheck ----------------
# 用最小宏定义替代 AMReX 头文件，避免解析整个 AMReX 带来的耗时
CPPCHECK_COMMON=(
    --enable=unusedFunction,warning,style,performance
    --std=c++20 --language=c++ --inline-suppr --quiet
    --suppress=missingIncludeSystem --suppress=missingInclude
    --template='{file}:{line}:{severity}:{id}:{message}'
    -I "$SRC"
    "-DAMREX_SPACEDIM=$DIM"
    -DAMREX_GPU_DEVICE= -DAMREX_GPU_HOST_DEVICE= -DAMREX_GPU_HOST= -DAMREX_GPU_GLOBAL=
    -DAMREX_FORCE_INLINE=inline -DAMREX_INLINE=inline
    "-DAMREX_D_DECL(a,b,c)=$DDECL" "-DAMREX_D_TERM(a,b,c)=a b c"
    "-DAMREX_ASSERT(x)=" "-DAMREX_ALWAYS_ASSERT(x)="
    "-DAMREX_ASSERT_WITH_MESSAGE(x,m)=" "-DAMREX_ALWAYS_ASSERT_WITH_MESSAGE(x,m)="
)
mapfile -t CPP_FILES < <(find "$SRC" -maxdepth 1 -type f \( -name '*.cpp' -o -name '*.cxx' -o -name '*.cc' \) | sort)
[ "${#CPP_FILES[@]}" -gt 0 ] || { echo "错误：$SRC 下没有 .cpp 文件" >&2; exit 1; }

# 主机配置与 CUDA 配置各跑一次：只在 #ifdef AMREX_USE_CUDA 分支中被调用的函数不应算死代码
echo "[3/3] cppcheck (host)"
"$VENV/bin/cppcheck" "${CPPCHECK_COMMON[@]}" "${CPP_FILES[@]}" 2> "$OUT/cppcheck_host.txt" || true
echo "[3/3] cppcheck (AMREX_USE_CUDA/AMREX_USE_GPU)"
"$VENV/bin/cppcheck" "${CPPCHECK_COMMON[@]}" -DAMREX_USE_CUDA -DAMREX_USE_GPU \
    --enable=unusedFunction "${CPP_FILES[@]}" 2> "$OUT/cppcheck_cuda.txt" || true

# ---------------- 汇总 ----------------
set +e
"$VENV/bin/python" -I - "$SRC" "$OUT" "$CCN" "$LENGTH" "${ALLOW:-}" "${THRESHOLD:-}" <<'PY'
import csv, json, os, re, sys
from collections import Counter

src, out, ccn_lim, len_lim, allow_path, threshold = sys.argv[1:7]
ccn_lim, len_lim = int(ccn_lim), int(len_lim)
rel = lambda p: os.path.relpath(p, src)

# 白名单：有意保留但暂未调用的接口
allow = set()
if allow_path:
    for line in open(allow_path, encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if line:
            allow.add(line)

# ---- jscpd ----
rep = json.load(open(os.path.join(out, "jscpd", "jscpd-report.json"), encoding="utf-8"))
tot = rep["statistics"]["total"]
dup_pct = float(tot["percentage"])
per_file = rep["statistics"]["formats"].get("cpp", {}).get("sources", {})
clones = sorted(rep["duplicates"], key=lambda d: -d["lines"])

# ---- lizard ----
funcs = []
with open(os.path.join(out, "lizard.csv"), encoding="utf-8", errors="replace") as f:
    for row in csv.reader(f):
        if len(row) < 11 or not row[0].isdigit():
            continue
        funcs.append(dict(nloc=int(row[0]), ccn=int(row[1]), length=int(row[4]),
                          file=rel(row[6]), name=row[7], start=int(row[9])))
hot = sorted([x for x in funcs if x["ccn"] > ccn_lim or x["length"] > len_lim],
             key=lambda x: (-x["ccn"], -x["nloc"]))
nloc_of = {}
for x in funcs:
    nloc_of.setdefault(x["name"].split("::")[-1], []).append(x)

# ---- cppcheck ----
pat = re.compile(r"^(.*?):(\d+):(\w+):(\w+):(.*)$")
def parse(path):
    items = []
    for line in open(path, encoding="utf-8", errors="replace"):
        m = pat.match(line.strip())
        if m:
            items.append(dict(file=rel(m[1]) if m[1].startswith("/") else m[1],
                              line=int(m[2]), sev=m[3], id=m[4], msg=m[5]))
    return items
host = parse(os.path.join(out, "cppcheck_host.txt"))
cuda = parse(os.path.join(out, "cppcheck_cuda.txt"))
fname = lambda it: re.search(r"'([^']+)'", it["msg"])[1]
cuda_unused = {fname(it) for it in cuda if it["id"] == "unusedFunction"}
unused, only_host = [], []
for it in host:
    if it["id"] != "unusedFunction":
        continue
    n = fname(it)
    if n in allow:
        continue
    (unused if n in cuda_unused else only_host).append((it, n))
others = [it for it in host if it["id"] != "unusedFunction"]

def dead_nloc(n):
    # 按函数名取 lizard 的 NLOC；同名重载取全部
    return sum(x["nloc"] for x in nloc_of.get(n.split("::")[-1], []))
dead_total = sum(dead_nloc(n) for _, n in unused)
total_nloc = sum(x["nloc"] for x in funcs) or 1

# ---- 写报告 ----
L = []
L.append(f"# 代码审计：{src}\n")
L.append("| 指标 | 值 |\n|---|---|")
L.append(f"| 分析文件数 | {tot['sources']} |")
L.append(f"| 总行数 | {tot['lines']} |")
L.append(f"| 重复率（行） | {dup_pct:.2f}% （{tot['duplicatedLines']} 行，{tot['clones']} 处） |")
L.append(f"| 复杂度报警函数 | {len(hot)} （CCN>{ccn_lim} 或长度>{len_lim}） |")
L.append(f"| 未调用函数候选 | {len(unused)} 个，约 {dead_total} NLOC（占函数体 {100*dead_total/total_nloc:.1f}%） |")
L.append(f"| cppcheck 其他告警 | {len(others)} |\n")

L.append("## 1. 重复率按文件\n\n| 文件 | 行数 | 重复行 | 重复率 |\n|---|---|---|---|")
for f, s in sorted(per_file.items(), key=lambda kv: -kv[1]["duplicatedLines"]):
    if s["duplicatedLines"]:
        L.append(f"| {rel(f) if f.startswith('/') else f} | {s['lines']} | {s['duplicatedLines']} | {s['percentage']:.1f}% |")
L.append("\n最大的 20 处克隆：\n\n| 行数 | 片段 A | 片段 B |\n|---|---|---|")
for d in clones[:20]:
    a, b = d["firstFile"], d["secondFile"]
    L.append(f"| {d['lines']} | {rel(a['name']) if a['name'].startswith('/') else a['name']}:{a['start']}-{a['end']} "
             f"| {rel(b['name']) if b['name'].startswith('/') else b['name']}:{b['start']}-{b['end']} |")

L.append(f"\n## 2. 复杂度报警\n\n| CCN | NLOC | 长度 | 函数 | 位置 |\n|---|---|---|---|---|")
for x in hot:
    L.append(f"| {x['ccn']} | {x['nloc']} | {x['length']} | `{x['name']}` | {x['file']}:{x['start']} |")

L.append("\n## 3. 未调用函数候选（主机与 CUDA 配置均未调用）\n")
L.append("删除前需人工确认：可能是有意保留的对比实现，或通过宏、函数指针调用。\n")
L.append("| NLOC | 函数 | 位置 |\n|---|---|---|")
for it, n in sorted(unused, key=lambda p: -dead_nloc(p[1])):
    L.append(f"| {dead_nloc(n)} | `{n}` | {it['file']}:{it['line']} |")
if only_host:
    L.append("\n仅主机配置未调用（CUDA 分支中有调用，不算死代码）：" +
             ", ".join(f"`{n}`" for _, n in only_host))
if allow:
    L.append(f"\n白名单已排除：{', '.join(sorted(allow))}")

L.append("\n## 4. cppcheck 其他告警\n")
cnt = Counter(f"{it['sev']}:{it['id']}" for it in others)
L.append(", ".join(f"{k} ×{v}" for k, v in cnt.most_common()) + "\n")
L.append("| 级别 | 类型 | 位置 | 说明 |\n|---|---|---|---|")
# warning 优先，其次 style，performance 建议放最后
order = {"error": 0, "warning": 1, "style": 2, "performance": 3, "portability": 4}
for it in sorted(others, key=lambda it: (order.get(it["sev"], 9), it["file"], it["line"])):
    L.append(f"| {it['sev']} | {it['id']} | {it['file']}:{it['line']} | {it['msg'].replace('|', '/')} |")

open(os.path.join(out, "summary.md"), "w", encoding="utf-8").write("\n".join(L) + "\n")

# 终端摘要
print(f"  重复率 {dup_pct:.2f}%  复杂度报警 {len(hot)}  未调用候选 {len(unused)} (~{dead_total} NLOC)  其他告警 {len(others)}")
if threshold and dup_pct > float(threshold):
    print(f"  重复率 {dup_pct:.2f}% 超过阈值 {threshold}%")
    sys.exit(2)
PY
rc=$?
set -e
echo "[audit] 报告：$OUT/summary.md"
exit "$rc"
