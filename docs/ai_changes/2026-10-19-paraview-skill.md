# ParaView 使用流程与核心脚本迁入项目 skill

## 目的与范围

将 Windows GUI 控制桥和超算独立批处理整理成可调用的项目 skill，统一维护使用方式与实现。
涉及仓库工具、入口文档以及 BOX3D_OSI 分析脚本的两个调用路径，不修改求解器或数值算法。

## 主要改动

- 新增 `.agents/skills/paraview/SKILL.md` 与展示元数据，按用户是否需要当前 GUI 选择模式；详细流程放入 references。
- 核心脚本、包锁和既有测试迁入 skill 的 scripts；批处理继续只加载桥模块定义，复用原中心线导出函数。
- Shell 入口改用 Git 定位仓库根，维持既有 `.tools` 环境及编译节点路由。Windows PS1 改用迁移后的远端客户端源码地址，保留 BOM/CRLF。
- 更新 README 和算例调用路径。旧 connect_paraview.ps1、paraview_bridge_client.py 留软链，兼容已下载到 Windows 的旧启动脚本；不保留重复实现。
- 迁移前 PS1 工作树的非执行权限继续保留在新文件工作树中，不作为本次权限变更提交。

## 验证

- skill-creator quick_validate 通过；两份 Bash 脚本语法、Python AST、本地 Markdown 链接、兼容软链及 PS1 BOM/CRLF 检查通过。
- 既有 7 项启动协议、生命周期、安装锁校验和节点路由测试全部通过；SSH 失败不会回退到登录节点执行。
- 迁移后安装入口实际转到编译节点，识别相同包锁并跳过重链，返回 ParaView 6.2.0。
- 真实批处理在 whshare-agent-1 重提 job613495/plt_256000（Re=1000，level 0–2），用时约 12.89 秒；1025 点全部有效，与迁移前批处理 CSV 的 9 列逐点最大绝对差为 0。
- 现场验证产物位于忽略的 `.tools/paraview-batch/skill-migration-check/`。此结果验证提取和迁移兼容性，不证明全三维数值正确性或收敛。
- 本环境没有 Windows ParaView/PowerShell；未声称完成迁移后 Windows GUI 启动联调。既有窗口不会自动加载源码更新。
