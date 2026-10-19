# 独立远端 ParaView 批处理与编译节点执行约束

- 目的：无需用户启动本机 ParaView 或控制桥，Agent 可自动发起独立 pvbatch 数据处理。
- 现状核验：系统 ParaView-EGL-5.11.1 的 CMake 配置为 PARAVIEW_USE_PYTHON=OFF，无 pvpython/pvbatch；宿主 Linux aarch64、glibc 2.28。
- 环境：在仓库忽略目录 .tools/paraview-batch 安装独立 conda-forge ParaView 6.2.0 应用及 Python 3.12；micromamba 2.9.0 固定上游 SHA256，255 个依赖包由带 SHA256 的显式锁文件固定，通过 TUNA 镜像获取，下载约 502 MB。不替换系统 ParaView、不修改用户 shell，也不把应用环境用于普通分析。
- 节点：用户明确要求长期 ParaView 不在登录节点运行。安装入口和运行入口固定 SSH 跳转 whshare-agent-1，跳转失败直接退出。最初只在登录节点写入环境安装文件，未在登录节点启动 ParaView；实际程序版本检查、数据提取及后续安装入口验证均在编译节点完成。
- 复用：把原桥客户端中无实例状态的 export_centerline 方法移为同文件函数，桥接和独立批处理调用同一函数；增加只加载定义的入口，批处理不创建 GUI 定时器、SSH 轮询或注册桥会话。不复制过滤器实现，也不增加替代数值路径。现有桥接默认行为保持不变。
- 生命周期：独立安装的状态以 conda-meta 包记录为唯一来源，与锁文件的包文件名/哈希严格比对；匹配则跳过重链并实际探测版本，包含 URL 编码文件名处理。运行时清除继承的动态库/Python/显示相关环境。一般后处理继续由仓库 .venv 驱动，仅 ParaView API 脚本由应用自带解释器执行。
- 验证：编译节点实际 pvbatch --version 为 6.2.0；真实 installer 重跑确认跳过重链并成功探测版本。桥启动/定义加载 4 项测试、批处理路由/SSH 失败不本地回退/参数引用/环境隔离/锁文件哈希处理 3 项测试通过，Python AST、shell bash -n、文档路径和暂存 diff 检查通过。提取函数迁移前后 AST 函数体相同。
- 数值证据：复用 job613495 的 plt_256000，在 whshare-agent-1 独立提取 1025 点；坐标、ux/uy/uz、arc_length、vtkGhostType、有效性掩码全部 9 列与先前桥接输出最大绝对差为 0（atol=1e-12、rtol=0）。批处理未访问用户本机窗口或固定桥入口。算例产物与记录在 BOX3D_OSI 内。
- 边界：证明该 PlotFile 的中心线数据提取可独立完成；不证明全三维数值等价或收敛。ParaView 自身的截图/动画渲染未测试，当前图由仓库 .venv 的 Matplotlib 生成。
