# ChannelFlow

## 概览
3D channel flow with immersed particles (AMR LBM)。

## 目录结构
- `src/` — 原始 C++/CUDA 源码，直接从学习项目复制。
- `config/` — 构建配置与 AMReX 输入文件。
- `scripts/` — 编译、清理与提交脚本。
- `data/` — 运行结果或分析用辅助数据（如有）。
- `docs/` — 附加文档（如有）。

## 同步说明
这些文件由 `tools/sync_projects.py` 自动同步。如需再次更新，请在仓库根目录运行：

```bash
python tools/sync_projects.py --project ChannelFlow
```

加入 `--clean` 可先清空目标目录，`--extra-exclude PATTERN` 可临时排除更多文件。
