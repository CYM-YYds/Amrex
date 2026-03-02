#!/bin/bash
# ccdb.sh
# ------------------------------------------------------------
# 编译与 compile_commands 生成模块。
#
# 关键职责：
#   - 组装 make 命令（含 AMREX_GIT_VERSION 策略）
#   - 按策略选择 bear/intercept/wrapper 生成编译数据库
#   - 在 wrapper 模式下生成临时编译器包装器并汇总 jsonl
#   - 清理临时文件并输出最终 compile_commands.json
#
# 维护建议：
#   - 若需改 clangd 兼容策略，优先改 wrapper 生成区与后处理 Python 段。
#   - 若需改“是否全量重建”策略，优先改 CCDB_REQUIRE_FULL 相关分支。

# 2) 执行编译并（可选）生成 compile_commands.json
do_compile() {
    # 统一返回 make_status，调用方据此决定后续动作。
    local make_status=0
    local make_cmd=( make -j"${MAKE_J}" CUDA_ARCH="${CUDA_ARCH}" )
    local amrex_git_version=""
    if [[ "${AMREX_VERSION_MODE}" == "release" ]]; then
        amrex_git_version="$(detect_amrex_release_from_project)"
        if [[ -n "$amrex_git_version" ]]; then
            make_cmd+=( "AMREX_GIT_VERSION=${amrex_git_version}" )
            info "AMREX_VERSION_MODE=release，固定 AMREX_GIT_VERSION=${amrex_git_version}"
        else
            info "AMREX_VERSION_MODE=release，但未解析到发布号，回退到 AMReX 默认 git describe。"
        fi
    fi
    if [ "${MAKE_KEEP_GOING}" = "1" ]; then
        make_cmd+=( -k )
    fi

    if [ "${GEN_CCDB}" = "1" ]; then
        mkdir -p "${CCDB_OUTPUT_DIR}"
        local CCDB_FILE="${CCDB_OUTPUT_DIR}/compile_commands.json"

        use_wrapper=false
        case "${CCDB_METHOD}" in
            wrapper) use_wrapper=true ;;
            bear)    if command -v bear >/dev/null 2>&1; then
                         info "使用 bear 捕获编译数据库..."; bear --output "$CCDB_FILE" -- "${make_cmd[@]}" || make_status=$? ;
                      else
                         info "未找到 bear，回退到 wrapper 方法。"; use_wrapper=true ;
                      fi ;;
            intercept) if command -v intercept-build >/dev/null 2>&1; then
                          info "使用 intercept-build 捕获编译数据库...";
                          intercept-build --override-compiler --output "$CCDB_FILE" "${make_cmd[@]}" || make_status=$?
                        else
                          info "未找到 intercept-build，回退到 wrapper 方法。"; use_wrapper=true ;
                        fi ;;
            auto|*)
                      if command -v bear >/dev/null 2>&1; then
                          info "使用 bear 捕获编译数据库..."; bear --output "$CCDB_FILE" -- "${make_cmd[@]}" || make_status=$?
                      elif command -v intercept-build >/dev/null 2>&1; then
                          info "使用 intercept-build 捕获编译数据库...";
                          intercept-build --override-compiler --output "$CCDB_FILE" "${make_cmd[@]}" || make_status=$?
                      else
                          use_wrapper=true
                      fi ;;
        esac

        if [ "${use_wrapper}" = true ]; then
            info "未检测到 bear/intercept-build，使用 PATH 包装 gcc/g++/nvcc 写入 JSON。"
            local PREV_CCDB=""
            if [[ -s "$CCDB_FILE" ]]; then
                PREV_CCDB="${CCDB_FILE}.prev.$$"
                cp -f "$CCDB_FILE" "$PREV_CCDB"
            fi
            rm -f .cc_commands.jsonl .cxx_commands.jsonl .nvcc_commands.jsonl
            local WRAPDIR="$(pwd)/.wrapbin"
            mkdir -p "${WRAPDIR}"

            export REAL_GCC="${REAL_GCC:-$(command -v gcc || echo /home/HPCBase/compilers/gcc/10.3.1/bin/gcc)}"
            export REAL_GXX="${REAL_GXX:-$(command -v g++ || echo /home/HPCBase/compilers/gcc/10.3.1/bin/g++)}"
            export REAL_GIT="${REAL_GIT:-$(command -v git || echo /usr/bin/git)}"
            export REAL_NVCC="${REAL_NVCC:-$(command -v nvcc || echo /usr/local/cuda/bin/nvcc)}"
            export REAL_MPICXX="${REAL_MPICXX:-$(command -v mpicxx || echo /home/HPCBase/mpi/hmpi/1.2.0-huawei-sp1/bin/mpicxx)}"
            export REAL_MPICC="${REAL_MPICC:-$(command -v mpicc || echo /home/HPCBase/mpi/hmpi/1.2.0-huawei-sp1/bin/mpicc)}"

            cat > "${WRAPDIR}/gcc" <<'EOS'
#!/usr/bin/env bash
out=.cc_commands.jsonl
args=("$@")
has_nvcc=0
has_c=0
for a in "${args[@]}"; do
    [[ "$a" == "-c" ]] && has_c=1
    [[ "$a" == --expt-* || "$a" == "--forward-unknown-to-host-compiler" || "$a" == -Xcudafe* || "$a" == -Xptxas* || "$a" == --diag_suppress* ]] && has_nvcc=1
done
filtered=()
skip_next=0
for ((i=0;i<${#args[@]};i++)); do
    if (( skip_next )); then skip_next=0; continue; fi
    case "${args[$i]}" in
        -MMD|-MP) continue ;;
        -MF) skip_next=1; continue ;;
    esac
    filtered+=("${args[$i]}")
done
json_escape() {
    local s="$1"
    s="${s//\\/\\\\}"; s="${s//\"/\\\"}"; s="${s//$'\n'/}"
    printf "%s" "$s"
}
if (( has_nvcc==0 && has_c==1 )); then
    src=""
    for a in "${filtered[@]}"; do
        case "$a" in *.c|*.cc|*.cpp|*.cxx|*.C) src="$a" ;; esac
    done
    if [[ -n "$src" ]]; then
        bname="$(basename -- "$src")"
        if [[ "$src" == /tmp/tmpxft_* ]] || [[ "$bname" == tmpxft_* ]] || [[ "$src" == *cudafe* ]]; then
            exec "$REAL_GCC" "${filtered[@]}"
        fi
    fi
    args_json="\"$(json_escape "$REAL_GCC")\""
    for a in "${filtered[@]}"; do args_json+=",\"$(json_escape "$a")\""; done
    printf '{"directory":"%s","arguments":[%s],"file":"%s"}\n' "$(pwd)" "$args_json" "$src" >> "$out"
fi
exec "$REAL_GCC" "${filtered[@]}"
EOS

            cat > "${WRAPDIR}/g++" <<'EOS'
#!/usr/bin/env bash
out=.cxx_commands.jsonl
args=("$@")
has_nvcc=0
has_c=0
for a in "${args[@]}"; do
    [[ "$a" == "-c" ]] && has_c=1
    [[ "$a" == --expt-* || "$a" == "--forward-unknown-to-host-compiler" || "$a" == -Xcudafe* || "$a" == -Xptxas* || "$a" == --diag_suppress* ]] && has_nvcc=1
done
filtered=()
skip_next=0
for ((i=0;i<${#args[@]};i++)); do
    if (( skip_next )); then skip_next=0; continue; fi
    case "${args[$i]}" in
        -MMD|-MP) continue ;;
        -MF) skip_next=1; continue ;;
    esac
    filtered+=("${args[$i]}")
done
json_escape() {
    local s="$1"
    s="${s//\\/\\\\}"; s="${s//\"/\\\"}"; s="${s//$'\n'/}"
    printf "%s" "$s"
}
if (( has_nvcc==0 && has_c==1 )); then
    src=""
    for a in "${filtered[@]}"; do
        case "$a" in *.c|*.cc|*.cpp|*.cxx|*.C) src="$a" ;; esac
    done
    if [[ -n "$src" ]]; then
        bname="$(basename -- "$src")"
        if [[ "$src" == /tmp/tmpxft_* ]] || [[ "$bname" == tmpxft_* ]] || [[ "$src" == *cudafe* ]]; then
            exec "$REAL_GXX" "${filtered[@]}"
        fi
    fi
    args_json="\"$(json_escape "$REAL_GXX")\""
    for a in "${filtered[@]}"; do args_json+=",\"$(json_escape "$a")\""; done
    printf '{"directory":"%s","arguments":[%s],"file":"%s"}\n' "$(pwd)" "$args_json" "$src" >> "$out"
fi
exec "$REAL_GXX" "${filtered[@]}"
EOS

            cat > "${WRAPDIR}/nvcc" <<'EOS'
#!/usr/bin/env bash
out=.nvcc_commands.jsonl
args=("$@")
has_c=0; src=""
for a in "${args[@]}"; do
    [[ "$a" == "-c" ]] && has_c=1
    case "$a" in *.c|*.cc|*.cpp|*.cxx|*.C|*.cu) src="$a" ;; esac
done
HOST_CXX="${REAL_GXX:-$(command -v g++ || echo g++)}"
for ((i=0;i<${#args[@]};i++)); do
    a="${args[$i]}"
    case "$a" in
        -ccbin=*) HOST_CXX="${a#-ccbin=}" ;;
        -ccbin)
            if (( i+1 < ${#args[@]} )); then HOST_CXX="${args[$((i+1))]}"; fi ;;
    esac
done
host_flags=()
have_std=0
for ((i=0;i<${#args[@]};i++)); do
    a="${args[$i]}"
    case "$a" in
        -I*|-isystem*|-D*|-O*|-g|-f*) host_flags+=("$a") ;;
        -I|-isystem)
            if (( i+1 < ${#args[@]} )); then
                nxt="${args[$((i+1))]}"
                if [[ -n "$nxt" && ! "$nxt" =~ ^- ]]; then
                    host_flags+=("$a" "$nxt")
                    ((i++))
                    continue
                fi
            fi
            ;;
        -m32|-m64|-march=*|-mtune=*|-mcpu=*|-mfpu=*|-mno-*|-mavx*|-msse*|-mfma*) host_flags+=("$a") ;;
        -maxrregcount|-maxrregcount=*) ;;
        -std=*) have_std=1; host_flags+=("$a") ;;
        -Xcompiler)
            if (( i+1 < ${#args[@]} )); then
                IFS=',' read -r -a xc <<< "${args[$((i+1))]}"
                for x in "${xc[@]}"; do [[ -n "$x" ]] && host_flags+=("$x"); done
            fi ;;
        -Xcompiler=*)
            IFS=',' read -r -a xc <<< "${a#-Xcompiler=}"
            for x in "${xc[@]}"; do [[ -n "$x" ]] && host_flags+=("$x"); done ;;
    esac
done
[[ $have_std -eq 0 ]] && host_flags+=("-std=c++17")
filtered=()
skip_next=0
for ((i=0;i<${#args[@]};i++)); do
    if (( skip_next )); then skip_next=0; continue; fi
    case "${args[$i]}" in -MMD|-MP) continue ;; -MF) skip_next=1; continue ;; esac
    filtered+=("${args[$i]}")
done
json_escape(){ s="$1"; s="${s//\\/\\\\}"; s="${s//\"/\\\"}"; s="${s//$'\n'/}"; printf "%s" "$s"; }
if (( has_c==1 )) && [[ -n "$src" ]]; then
    args_json="\"$(json_escape "$HOST_CXX")\""
    for a in "${host_flags[@]}"; do args_json+=",\"$(json_escape "$a")\""; done
    args_json+=",\"-c\""
    args_json+=",\"$(json_escape "$src")\""
    printf '{"directory":"%s","arguments":[%s],"file":"%s"}\n' "$(pwd)" "$args_json" "$src" >> "$out"
fi
exec "$REAL_NVCC" "${filtered[@]}"
EOS

            chmod +x "${WRAPDIR}/gcc" "${WRAPDIR}/g++" "${WRAPDIR}/nvcc"
            export PATH="${WRAPDIR}:$PATH"

            cat > "${WRAPDIR}/mpicxx" <<'EOS'
#!/usr/bin/env bash
out=.nvcc_commands.jsonl
args=("$@")
has_c=0; src=""
for a in "${args[@]}"; do
    [[ "$a" == "-c" ]] && has_c=1
    case "$a" in *.c|*.cc|*.cpp|*.cxx|*.C|*.cu) src="$a" ;; esac
done
HOST_CXX="${REAL_GXX:-$(command -v g++ || echo g++)}"
for ((i=0;i<${#args[@]};i++)); do
    a="${args[$i]}"
    case "$a" in
        -ccbin=*) HOST_CXX="${a#-ccbin=}" ;;
        -ccbin)
            if (( i+1 < ${#args[@]} )); then HOST_CXX="${args[$((i+1))]}"; fi ;;
    esac
done
host_flags=()
have_std=0
for ((i=0;i<${#args[@]};i++)); do
    a="${args[$i]}"
    case "$a" in
        -I*|-isystem*|-D*|-O*|-g|-f*) host_flags+=("$a") ;;
        -I|-isystem)
            if (( i+1 < ${#args[@]} )); then
                nxt="${args[$((i+1))]}"
                if [[ -n "$nxt" && ! "$nxt" =~ ^- ]]; then
                    host_flags+=("$a" "$nxt")
                    ((i++))
                    continue
                fi
            fi
            ;;
        -m32|-m64|-march=*|-mtune=*|-mcpu=*|-mfpu=*|-mno-*|-mavx*|-msse*|-mfma*) host_flags+=("$a") ;;
        -maxrregcount|-maxrregcount=*) ;;
        -std=*) have_std=1; host_flags+=("$a") ;;
        -Xcompiler)
            if (( i+1 < ${#args[@]} )); then
                IFS=',' read -r -a xc <<< "${args[$((i+1))]}"
                for x in "${xc[@]}"; do [[ -n "$x" ]] && host_flags+=("$x"); done
            fi ;;
        -Xcompiler=*)
            IFS=',' read -r -a xc <<< "${a#-Xcompiler=}"
            for x in "${xc[@]}"; do [[ -n "$x" ]] && host_flags+=("$x"); done ;;
    esac
done
[[ $have_std -eq 0 ]] && host_flags+=("-std=c++17")
filtered=()
skip_next=0
for ((i=0;i<${#args[@]};i++)); do
    if (( skip_next )); then skip_next=0; continue; fi
    case "${args[$i]}" in -MMD|-MP) continue ;; -MF) skip_next=1; continue ;; esac
    filtered+=("${args[$i]}")
done
json_escape(){ s="$1"; s="${s//\\/\\\\}"; s="${s//\"/\\\"}"; s="${s//$'\n'/}"; printf "%s" "$s"; }
if (( has_c==1 )) && [[ -n "$src" ]]; then
    args_json="\"$(json_escape "$HOST_CXX")\""
    for a in "${host_flags[@]}"; do args_json+=",\"$(json_escape "$a")\""; done
    args_json+=",\"-c\""
    args_json+=",\"$(json_escape "$src")\""
    printf '{"directory":"%s","arguments":[%s],"file":"%s"}\n' "$(pwd)" "$args_json" "$src" >> "$out"
fi
exec "$REAL_MPICXX" "${filtered[@]}"
EOS

            cat > "${WRAPDIR}/mpicc" <<'EOS'
#!/usr/bin/env bash
exec "$REAL_MPICC" "$@"
EOS
            chmod +x "${WRAPDIR}/mpicxx" "${WRAPDIR}/mpicc"

            cat > "${WRAPDIR}/git" <<'EOS'
#!/usr/bin/env bash
REAL_GIT_BIN="${REAL_GIT:-$(command -v git || echo /usr/bin/git)}"
subcmd="$1"
case "$subcmd" in
    describe|rev-parse)
        out="$($REAL_GIT_BIN "$@" 2>/dev/null || true)"
        if [ -n "$out" ]; then
            printf "%s\n" "$out"
            exit 0
        else
            exit 0
        fi
        ;;
    *)
        exec "$REAL_GIT_BIN" "$@"
        ;;
esac
EOS
            chmod +x "${WRAPDIR}/git"

            "${make_cmd[@]}" || make_status=$?
            if [[ ! -s .cc_commands.jsonl && ! -s .cxx_commands.jsonl ]]; then
                if [[ -n "$PREV_CCDB" && "${CCDB_REQUIRE_FULL}" != "1" ]]; then
                    mv -f "$PREV_CCDB" "$CCDB_FILE"
                    rm -f .cc_commands.jsonl .cxx_commands.jsonl .nvcc_commands.jsonl
                    rm -rf "${WRAPDIR}"
                    info "未捕获到编译步骤，复用已有 compile_commands.json（跳过 -B 全量重建）。"
                    return "${make_status}"
                fi
                info "未捕获到编译步骤，强制重建以生成编译数据库..."
                if [[ -n "$amrex_git_version" ]]; then
                    make -B -j"${MAKE_J}" CUDA_ARCH="${CUDA_ARCH}" AMREX_GIT_VERSION="${amrex_git_version}" || make_status=$?
                else
                    make -B -j"${MAKE_J}" CUDA_ARCH="${CUDA_ARCH}" || make_status=$?
                fi
            fi
            local NEW_CCDB="${CCDB_FILE}.new.$$"
            echo '[' > "$NEW_CCDB"
            local first=1
            for f in .cc_commands.jsonl .cxx_commands.jsonl .nvcc_commands.jsonl; do
                if [[ -f "$f" ]]; then
                    while IFS= read -r line; do
                        if [[ $first -eq 1 ]]; then first=0; else echo ',' >> "$NEW_CCDB"; fi
                        echo "$line" >> "$NEW_CCDB"
                    done < "$f"
                fi
            done
            echo ']' >> "$NEW_CCDB"

            rm -f .cc_commands.jsonl .cxx_commands.jsonl .nvcc_commands.jsonl
            rm -rf "${WRAPDIR}"

        python3 - "$NEW_CCDB" <<'PY' 2>/dev/null || true
import re, json, shlex, sys
p = sys.argv[1] if len(sys.argv) > 1 else 'compile_commands.json'
try:
        txt = open(p,'r',encoding='utf-8').read()
except Exception:
        raise SystemExit(0)
changed = False
def repl(m):
        global changed
        s = m.group(1)
        try:
                tokens = shlex.split(s)
        except Exception:
                tokens = []
        changed = True
        return '"arguments":' + json.dumps(tokens, ensure_ascii=False) + ',"file"'
new = re.sub(r'"command":"(.*?)","file"', repl, txt, flags=re.DOTALL)
if changed:
        open(p,'w',encoding='utf-8').write(new)
PY

            python3 - "$NEW_CCDB" <<'PY' 2>/dev/null || true
import json, re, os, sys
p=sys.argv[1] if len(sys.argv) > 1 else 'compile_commands.json'
try:
    db=json.load(open(p,'r',encoding='utf-8'))
except Exception:
    raise SystemExit(0)

def split_tokens(args):
    out=[]
    for t in args:
        parts=str(t).split()
        out.extend(parts)
    return out

def clean_tokens(args):
    out=[]; i=0; n=len(args)
    while i<n:
        t=args[i]
        if (t.startswith('-Xptxas') or t.startswith('-Xcudafe') or t.startswith('--expt-') or
            t=='--forward-unknown-to-host-compiler' or t.startswith('--diag_suppress')):
            i+=1; continue
        if t.startswith('-gencode') or t.startswith('-arch') or t.startswith('--device-'):
            i+=1; continue
        if t.startswith('-D__CUDA_ARCH__') or t.startswith('-D__CUDA_ARCH_LIST__'):
            i+=1; continue
        if t=='-maxrregcount':
            i+=2; continue
        if t.startswith('-maxrregcount='):
            i+=1; continue
        if t=='-isystem':
            nxt=args[i+1] if i+1<n else None
            if (nxt is None) or (str(nxt).startswith('-')):
                i+=1; continue
            else:
                out.append(t); out.append(nxt); i+=2; continue
        out.append(t); i+=1
    return out

changed=False
for e in db:
    args=e.get('arguments')
    if not isinstance(args,list):
        continue
    a1=split_tokens(args)
    a2=clean_tokens(a1)
    if a2!=args:
        e['arguments']=a2
        changed=True

keep_tmp = os.environ.get('CCDB_KEEP_TMP','0') == '1'
if not keep_tmp:
    def is_tmpxft(entry):
        f=str(entry.get('file',''))
        return f.startswith('/tmp/tmpxft_') or ('cudafe' in f)
    new_db=[e for e in db if not is_tmpxft(e)]
    if len(new_db)!=len(db):
        db=new_db
        changed=True

dedup = os.environ.get('CCDB_DEDUP_BY_FILE','1') == '1'
if dedup:
    by_file={}
    for e in db:
        f=e.get('file')
        if f:
            by_file[f]=e
    db=list(by_file.values())

if changed:
    json.dump(db, open(p,'w',encoding='utf-8'), ensure_ascii=False, indent=2)
PY

            if [[ -n "$PREV_CCDB" && "${CCDB_MERGE_OLD}" = "1" && -s "$PREV_CCDB" ]]; then
                python3 - "$PREV_CCDB" "$NEW_CCDB" "$CCDB_FILE" <<'PY' 2>/dev/null || true
import json, sys
oldf, newf, outf = sys.argv[1], sys.argv[2], sys.argv[3]
try:
    old = json.load(open(oldf, 'r', encoding='utf-8'))
except Exception:
    old = []
try:
    new = json.load(open(newf, 'r', encoding='utf-8'))
except Exception:
    new = []

def key(e):
    return (e.get('file'), e.get('directory'))

merged = {}
for e in old:
    k = key(e)
    if k[0]:
        merged[k] = e
for e in new:
    k = key(e)
    if k[0]:
        merged[k] = e

db = list(merged.values())
json.dump(db, open(outf, 'w', encoding='utf-8'), ensure_ascii=False, indent=2)
PY
                rm -f "$PREV_CCDB" "$NEW_CCDB"
            else
                mv -f "$NEW_CCDB" "$CCDB_FILE"
                rm -f "$PREV_CCDB"
            fi
            info "compile_commands.json 已生成到: ${CCDB_FILE}"
    fi
    else
    info "GEN_CCDB=0，跳过生成编译数据库"
    "${make_cmd[@]}" || make_status=$?
    fi

    return "${make_status}"
}
