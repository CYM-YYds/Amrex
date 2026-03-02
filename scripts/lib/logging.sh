#!/bin/bash
# logging.sh
# ------------------------------------------------------------
# 日志模块：
#   - setup_logging   初始化日志目录/重定向/退出钩子
#   - rotate_logs     按数量清理旧日志
#   - generate_summary 从完整日志提取错误与警告摘要
#
# 说明：
#   - setup_logging 可能会重定向 stdout/stderr，请在其后输出关键日志。
#   - summary 仅做“提炼”，不影响主流程返回码。

generate_summary() {
    # 在脚本退出时执行（由 setup_logging 里的 trap 注册）。
    if [[ "$LOG_TO_FILE" != "1" || "$LOG_SUMMARY" != "1" ]]; then
        return 0
    fi
    if [[ -z "$LOGFILE" || ! -f "$LOGFILE" ]]; then
        return 0
    fi
    summary_file="${LOGFILE%.log}-summary.log"

    LOG_ERRORS_PATTERN="error:|Error |ERROR |error |Error:|FATAL:|fatal |undefined reference|cannot find|No such file|Permission denied|Segmentation fault|cannot open|make: \*\*\*|failed|FAILED"

    SEP_LINE="$(printf '%.0s=' {1..80})"
    ERROR_MARK="❌ ERROR"
    WARN_MARK="⚠️  WARN"
    INFO_MARK="ℹ️  INFO"
    LINK_MARK="🔗 LINK"
    FILE_MARK="📁 FILE"
    STAT_MARK="📊 STAT"

    {
        echo "$SEP_LINE"
        echo "编译摘要 - $(date '+%Y-%m-%d %H:%M:%S')"
        echo "$SEP_LINE"
        echo ""

        echo "$ERROR_MARK 编译错误"
        echo "$SEP_LINE"
        if grep -Ei "error:|Error |ERROR |error |Error:|FATAL:|fatal " "$LOGFILE" >/dev/null 2>&1; then
            grep -Ei "error:|Error |ERROR |error |Error:|FATAL:|fatal " -A 2 -B 2 "$LOGFILE" | head -n "$LOG_SUMMARY_LINES"
        else
            echo "(无编译错误)"
        fi
        echo ""

        echo "$LINK_MARK 链接错误"
        echo "$SEP_LINE"
        if grep -Ei "undefined reference|cannot find -l" "$LOGFILE" >/dev/null 2>&1; then
            grep -Ei "undefined reference|cannot find -l" -A 2 -B 2 "$LOGFILE" | head -n "$LOG_SUMMARY_LINES"
        else
            echo "(无链接错误)"
        fi
        echo ""

        echo "$FILE_MARK 文件错误"
        echo "$SEP_LINE"
        if grep -Ei "No such file|cannot open" "$LOGFILE" >/dev/null 2>&1; then
            grep -Ei "No such file|cannot open" -A 2 -B 2 "$LOGFILE" | head -n "$LOG_SUMMARY_LINES"
        else
            echo "(无文件错误)"
        fi
        echo ""

        echo "$WARN_MARK 警告信息"
        echo "$SEP_LINE"
        if grep -Ei "warning:|Warning |WARNING " "$LOGFILE" >/dev/null 2>&1; then
            grep -Ei "warning:|Warning |WARNING " -A 2 -B 2 "$LOGFILE" | head -n "$LOG_SUMMARY_LINES"
        else
            echo "(无警告信息)"
        fi
        echo ""

        echo "$STAT_MARK 错误统计"
        echo "$SEP_LINE"
        echo "  编译错误数量: $(grep -ci "error:" "$LOGFILE")"
        echo "  链接错误数量: $(grep -ci "undefined reference" "$LOGFILE")"
        echo "  警告数量:    $(grep -ci "warning:" "$LOGFILE")"
        echo ""

        if ! grep -Ei "$LOG_ERRORS_PATTERN" "$LOGFILE" >/dev/null 2>&1; then
            echo "$INFO_MARK 编译成功 - 日志末尾输出"
            echo "$SEP_LINE"
            tail -n "$LOG_SUMMARY_LINES" "$LOGFILE"
        fi

        echo ""
        echo "$SEP_LINE"
        echo "提示: 完整日志详见 $(basename "$LOGFILE")"
        echo "$SEP_LINE"
    } > "$summary_file"

    ln -sf "$summary_file" "${LOG_DIR}/compile-latest-summary.log" || true
    info "已生成分类日志摘要: $summary_file"
}

rotate_logs() {
    # 按时间倒序保留最近 LOG_MAX_FILES 份构建日志。
    if [[ ! -d "$LOG_DIR" ]]; then
        return 0
    fi
    mapfile -t files < <(ls -1t "$LOG_DIR"/compile-*.log 2>/dev/null || true)
    if [[ ${#files[@]} -le $LOG_MAX_FILES ]]; then
        return 0
    fi
    for ((i=LOG_MAX_FILES; i<${#files[@]}; i++)); do
        rm -f "${files[$i]}" || true
        rm -f "${files[$i]%.log}-summary.log" || true
    done
}

setup_logging() {
    # 统一处理日志配置，避免主入口散落日志初始化细节。
    LOG_TO_FILE="${LOG_TO_FILE:-1}"
    LOG_TEE="${LOG_TEE:-1}"
    LOG_DIR="${LOG_DIR:-$PROJECT_ROOT/logs}"
    LOG_MAX_FILES="${LOG_MAX_FILES:-3}"
    LOG_SUMMARY="${LOG_SUMMARY:-1}"
    LOG_SUMMARY_LINES="${LOG_SUMMARY_LINES:-200}"
    LOG_ERRORS_PATTERN="error:|Error |ERROR |error |Error:|FATAL:|fatal |undefined reference|cannot find|No such file|Permission denied|Segmentation fault|cannot open|make: \*\*\*|failed|FAILED"

    if [[ "$LOG_TO_FILE" == "1" ]]; then
        mkdir -p "$LOG_DIR"
        _cf_ts="$(date '+%Y%m%dT%H%M%S')"
        LOGFILE="$LOG_DIR/compile-${_cf_ts}.log"
        rotate_logs
        ln -sf "$LOGFILE" "$LOG_DIR/compile-latest.log" || true
        trap generate_summary EXIT
        if [[ "$LOG_TEE" == "1" ]]; then
            exec > >(tee -a "$LOGFILE") 2>&1
        else
            exec > "$LOGFILE" 2>&1
        fi
        info "日志输出已重定向到: $LOGFILE"
    else
        info "LOG_TO_FILE=0: 未启用日志文件写入，输出仍在终端显示。"
    fi
}
