# BOX3D README 改写设计

## 目标

将 `projects/3Dcases/Computing_performance_test/BOX3D/README.md` 改写为中文，消除与仓库根目录 README 重复的通用说明，并准确说明该算例的定位：三维方腔流 AMR-LBM 的耗时分析与性能归因。

## 方案比较

1. 保留现有详细内容并逐段翻译：信息最完整，但会继续包含通用构建说明，且 README 过长。
2. 精简为算例概览并链接详细文档：避免与根目录重复，保留性能分析入口。采用此方案。
3. 仅保留一句算例简介：最简短，但无法说明计时输出和分析资料的位置。

## 文档结构

1. **算例定位**：说明这是基于 AMReX 的三维方腔流 AMR-LBM 算例，核心用途是分析各计算阶段耗时，而非提供通用仓库介绍。
2. **运行与计时口径**：仅保留与此算例直接相关的 1000 步性能窗口、`perf(s)`、`perf_detail(s)` 和 `perf_count` 的含义；不重复仓库级编译环境与目录说明。
3. **深入分析入口**：链接至 `docs/performance_profiling.md` 和 `docs/amr_grid_communication.md`，说明 TinyProfiler 结果适合耗时归因而非生产吞吐对比。

## 验证

检查 Markdown 标题层级、相对链接和代码字段是否与 `src/main.cpp` 的实际输出一致；本次仅修改文档，不运行编译或数值计算。
