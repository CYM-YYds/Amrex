# 脚本整理与维护规则知识收尾

范围：收尾 `d05c960`、`458f653`、`551ebce` 的脚本入口整理、后处理整合与轻量维护约定。
不审计其他算例源码，不重跑 CFD、远端 ParaView 或历史数值实验，不修改生成记忆。

校正：

- 根规则明确 Shell/Python 各自语法检查，独立临时脚本删除检查引用、复现用途与 diff，
  与轻量维护 Skill 一致；AGENTS 仍为 CLAUDE 软链。
- 算例及后处理 README 区分通用绘图与专用提取入口：后者固定 Re=1000/3200、U_lid=0.05，
  根据运行目录与最大步数生成输出目录，不支持 --output；复用导出仍会重写派生文件。

核对：根与算例 README、现役脚本/归档索引、交接状态、Skill 指令及旧入口引用。
旧绘图名仅保留在替换表与历史记录中；原数值验证证据沿用对应变更记录，不重复验收。
完成机械 Markdown 盘点、当前 CLI 帮助检查、相关文档链接与规则软链检查、Skill 校验和 diff 检查。

状态：代码/本地CLI verified-current；文档/规则 changed-and-verified；部署 not-applicable；
其他算例与求解器数值验收 out-of-scope。生成记忆只读，其中旧收敛入口命令已过时，
以现役后处理 README 为准；未获明确记忆写入请求，未写 correction input。
Git 只有当前 detached worktree，改动按仓库规则本地提交，不推送；没有删除分支或工作树。
本轮回归临时目录 `/tmp/box3d-plot-consolidation-ob_a5ju9` 与
`/tmp/box3d-plot-consolidation-yc8hk53e` 仅预览并保留，清场待汇报后用户确认。
