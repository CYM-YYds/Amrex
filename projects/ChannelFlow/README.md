# ChannelFlow

## 概览
3D channel flow with immersed particles (AMR LBM)。

## 目录结构
- `src/` — 原始 C++/CUDA 源码，直接从学习项目复制。
- `config/` — 构建配置与 AMReX 输入文件。
- `scripts/` — 编译、清理与提交脚本。
- `data/` — 所有模拟输出数据文件（粒子数据、速度剖面、绘图文件等）。
- `docs/` — 附加文档（如有）。

**注意**：所有模拟输出文件现在都会写入 `data/` 目录。详见 `DATA_OUTPUT_CHANGES.md`。

## 编译
在项目根目录执行脚本即可完成远程编译，并自动解析目录结构：

```bash
./scripts/compile.sh
```

如需调整并行度、CUDA 架构或生成 `compile_commands.json`，可通过环境变量（`MAKE_J`、`CUDA_ARCH`、`GEN_CCDB` 等）覆盖默认配置。

## 提交作业
使用 `dsub` 命令提交作业到计算节点：

```bash
cd /path/to/ChannelFlow
dsub scripts/submit.sh
```

脚本会自动：
1. 检测并切换到项目根目录
2. 加载必要的环境模块（MPI、CUDA、GCC）
3. 从调度系统获取节点分配信息
4. 生成 hostfile 并启动 MPI 作业
5. 执行 `main3d.gnu.MPI.CUDA.ex config/inputs`
6. 输出日志写入 `<JobID>-out.log`

**注意**：
- 脚本必须通过调度系统（`dsub`）提交，不能在登录节点直接运行
- 输入文件位于 `config/inputs`
- 所有输出数据将自动保存到 `data/` 目录

## 同步说明
这些文件由 `tools/sync_projects.py` 自动同步。如需再次更新，请在仓库根目录运行：

```bash
python tools/sync_projects.py --project ChannelFlow
```

加入 `--clean` 可先清空目标目录，`--extra-exclude PATTERN` 可临时排除更多文件。
