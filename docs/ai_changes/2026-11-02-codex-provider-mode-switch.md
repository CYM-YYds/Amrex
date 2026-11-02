# Codex provider 模式切换脚本

- 目的：简化 Codex 官方账号与第三方 API provider 的切换，避免手改 `config.toml` 后遗漏会话同步。
- 影响范围：仓库级 `scripts/` 和根 README；不直接修改仓库外的 Codex 配置。
- 主要改动：新增 `scripts/codex-provider-mode.sh`，无参数时在 `openai` 与 `custom` 间切换，也支持指定目标；复用 `codex-provider switch` 的备份和会话同步逻辑。
- 验证：`bash -n`、帮助输出、模拟当前 provider 为 `custom` 时切换到 `openai`、`git diff --check` 均通过；真实配置未执行切换。
- 操作边界：脚本切换和同步后提示在 VS Code 命令面板执行 `Developer: Reload Window`；当前远端 `code` CLI 未提供调用任意界面命令的参数。
