# ParaView 背景模式恢复兼容修复

- 目的：现场控制桥已成功读取管线并设置背景，但恢复模式时属性字符串被 ParaView 拒绝。
- 范围：仅控制桥客户端。
- 改动：保存底层 BackgroundColorMode 枚举整数，并通过底层属性恢复及更新代理，避免字符串表示往返问题。
- 验证：仓库 .venv Python 语法解析、带引号字符串属性场景的整数枚举恢复回归检查通过；暂存 diff 检查通过。
- 现场边界：当前旧客户端返回 inspect 和 background 成功，restore_background 失败；新客户端恢复逻辑尚需 Windows 现场验证。失败的恢复已先赋回背景 RGB，背景模式尚不能宣称恢复成功。
