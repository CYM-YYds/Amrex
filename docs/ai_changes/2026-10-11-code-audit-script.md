# 算例源码静态审计脚本

- 目的：用数字追踪 AI 辅助开发积累的重复代码、过长函数和未调用函数，为定期消重提供输入。
- 文件：`scripts/code_audit.sh`；工具安装在 `.tools/code_audit/`（已被 `.gitignore` 忽略）。
- 内容：
  - `--install` 安装固定版本工具：jscpd 4.0.5、lizard 1.17.13、cppcheck wheel 1.5.3（Cppcheck 2.22.0）。
  - jscpd 显式纳入 `.H` 并放开大文件上限，否则会漏掉 `Kernels.H`。
  - cppcheck 用最小 AMReX 宏定义代替 AMReX 头文件；主机与 `AMREX_USE_CUDA` 两种配置下都未调用的函数才列为候选。
  - 支持白名单（`-a`）与重复率阈值（`-t`，超过时返回 2），报告写到 `/tmp/code_audit/<算例>/summary.md`。
  - 只读，不修改源码。
- 验证：`bash -n` 通过；在全新 `.tools/code_audit` 完成安装；对 `BOX3D_OSI/src` 运行约 85 秒，得到重复率 8.64%、复杂度报警 13 个、未调用函数候选 35 个（约 2482 NLOC），与手工运行三个工具的结果一致；`-t 5` 返回码为 2。
