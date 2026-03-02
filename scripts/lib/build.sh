#!/bin/bash
# build.sh
# ------------------------------------------------------------
# 构建调度模块：
#   - verify_artifacts  编译后产物存在性提示
#   - run_here          本机执行完整编译流程
#   - run_build_entry   根据主机名决定本地执行或远端编译
#
# 说明：
#   - 远端编译固定节点 whshare-agent-1，与历史流程保持一致。
#   - 提交逻辑（SUBMIT_AFTER）在编译成功后执行，失败不触发。

verify_artifacts() {
    # 仅提示，不改变返回码。
    local found=0
    shopt -s nullglob
    for f in main*.ex *.ex; do
        if [[ -x "$f" ]]; then
            info "检测到可执行文件: $f"
            found=1
            break
        fi
    done
    shopt -u nullglob
    if [[ $found -eq 0 ]]; then
        if compgen -G "tmp_build_dir/o/*" >/dev/null 2>&1; then
            info "未发现 .ex 可执行，但检测到目标文件已生成（tmp_build_dir/o/）。可能是只编译了部分目标或增量构建。"
        else
            info "make 返回 0，但未检测到可执行或目标文件。请确认默认目标是否生成可执行，或是否指定了其他目标。"
        fi
    fi
}

run_here() {
    # 本地流程：加载环境 -> do_compile -> 校验产物 -> 可选提交。
    info "加载编译环境..."
    load_environment
    info "项目根目录: ${PROJECT_ROOT}"
    info "环境已加载，开始编译 (J=${MAKE_J}, CUDA_ARCH=${CUDA_ARCH})..."
    if do_compile; then
        info "编译成功！"
        verify_artifacts
        if [[ "${SUBMIT_AFTER}" == "1" ]]; then
            info "按请求在 ${SUBMIT_HOST} 上执行提交命令: ${SUBMIT_CMD}"
            read -r -a _cf_submit_ssh <<< "${SUBMIT_SSH}"
            if "${_cf_submit_ssh[@]}" "${SUBMIT_HOST}" "cd ${PROJECT_ROOT} && ${SUBMIT_CMD}"; then
                info "提交命令执行完成。"
            else
                echo "提交命令执行失败" >&2
            fi
        fi
    else
        rc=$?
        echo "编译失败，请检查错误信息。" >&2
        return "$rc"
    fi
}

run_build_entry() {
    # 入口调度：本机是编译节点则直接跑，否则 ssh 到编译节点执行同脚本。
    if [[ "$(hostname)" == "whshare-agent-1" ]]; then
        info "已在编译节点 whshare-agent-1 上，直接编译..."
        run_here
        exit $?
    else
        info "正在连接到编译节点 whshare-agent-1..."
        _orig_submit_after="$SUBMIT_AFTER"
        if [[ -z "$SCRIPT_PAYLOAD" || ! -f "$SCRIPT_PAYLOAD" ]]; then
            echo "错误: 无法找到脚本文件 $SCRIPT_PAYLOAD" >&2
            exit 1
        fi
        ssh_workdir=$(printf '%q' "$PROJECT_ROOT")
        ssh_make_j=$(printf '%q' "$MAKE_J")
        ssh_cuda_arch=$(printf '%q' "$CUDA_ARCH")
        ssh_gen_ccdb=$(printf '%q' "$GEN_CCDB")
        ssh_ccdb_method=$(printf '%q' "$CCDB_METHOD")
        ssh_make_keep=$(printf '%q' "$MAKE_KEEP_GOING")
        ssh_amrex_version_mode=$(printf '%q' "$AMREX_VERSION_MODE")
        ssh_amrex_git_version_override=$(printf '%q' "$AMREX_GIT_VERSION_OVERRIDE")
        ssh_submit_after=$(printf '%q' "0")
        ssh_submit_cmd=$(printf '%q' "$SUBMIT_CMD")
        ssh_submit_host=$(printf '%q' "$SUBMIT_HOST")
        ssh_submit_ssh=$(printf '%q' "$SUBMIT_SSH")
        ssh_project_root=$(printf '%q' "$PROJECT_ROOT")
        ssh_script_dir=$(printf '%q' "$SCRIPT_DIR")
        ssh whshare-agent-1 "cd ${ssh_workdir}; \
            MAKE_J=${ssh_make_j} \
            CUDA_ARCH=${ssh_cuda_arch} \
            GEN_CCDB=${ssh_gen_ccdb} \
            CCDB_METHOD=${ssh_ccdb_method} \
            MAKE_KEEP_GOING=${ssh_make_keep} \
            AMREX_VERSION_MODE=${ssh_amrex_version_mode} \
            AMREX_GIT_VERSION_OVERRIDE=${ssh_amrex_git_version_override} \
            SUBMIT_AFTER=${ssh_submit_after} \
            SUBMIT_CMD=${ssh_submit_cmd} \
            SUBMIT_HOST=${ssh_submit_host} \
            SUBMIT_SSH=${ssh_submit_ssh} \
            _CF_SCRIPT_DIR=${ssh_script_dir} \
            _CHANNELFLOW_PROJECT_ROOT=${ssh_project_root} \
            bash -s" < "${SCRIPT_PAYLOAD}"
        rc=$?
        if [ $rc -eq 0 ]; then
            info "远程编译成功。"
            if [[ "${_orig_submit_after}" == "1" ]]; then
                info "编译完成后在本地主机执行提交命令: ${SUBMIT_CMD}"
                read -r -a _cf_submit_cmd <<< "${SUBMIT_CMD}"
                if (cd "${PROJECT_ROOT}" && "${_cf_submit_cmd[@]}"); then
                    info "提交命令执行完成。"
                else
                    echo "提交命令执行失败" >&2
                fi
            fi
        else
            echo "远程编译失败 (exit $rc)。" >&2
        fi
        exit $rc
    fi
}
