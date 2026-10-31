# 移除独立单层外层调度

目的：删除此前无明显稳定收益的单层专用调度分叉，减少main的路径选择；保留已有独立收益证据的减少同步及无mask碰撞。未使用git revert整笔撤销历史提交，避免撤回后续有效优化和当前e9258b8计时/验证整理。

main统一经过RegridIfNeeded → Cycle2 → AdvanceLevel；移除performance.uniform_grid_path及path=uniform/amr日志选择。RegridIfNeeded条件要求配置maxLevel()>0，单层无需重建不变缓存；配置允许细化但当前无细层时仍会重网格。Cycle2原有finestLevel条件自然限制填ghost、递归、平均。不增加替代推进函数或状态。

performance.uniform_reduce_sync、lbm.uniform_nomask_collision、GPU事件统计、128线程和坐标复用均不改动。前两项不依赖独立main入口；此前无mask单GPU约1.2%–1.8%稳定收益、减少同步部分布局约11–16微秒/步，来源及边界见[旧独立对照](2026-10-29-uniform-sync-mask-tests.md)，不冒充当前版本性能。旧uniform_grid_path参数不再读取，历史脚本的开关性能对照仅适用于对应冻结版本。

验证仅一次CUDA三维FLOAT构建和64³/16块、周期/非周期各160步的逐值回归，复用已有独立冻结checkpoint；regrid_int=32以覆盖统一入口的重网格判断。检查减少同步/无mask实际开关仍为1/1、GPU统计每窗80样本。未重跑性能矩阵，不新增当前加速比；AMR数值核和多层Cycle2行为未改，不扩大宣称数值验收范围。

job621977成功完成构建与两次回归：各组valid/active L1/L2/Linf均有限且为0；日志确认实际开关1/1、两个窗口各80样本。正式FLOAT程序已更新为SHA256 4b670202083632f4869a447a6c7b4f8281dd1e504db1da7ee2ebe921e7ae3c3a，src与构建快照逐文件一致。仅main.cpp的求解器行为改变，既有计时/comparator整理保留；README和当前状态已同步。

Shell语法、Python AST、文档链接及保留CRLF的diff检查通过。本轮只覆盖三维FLOAT单rank两类单层边界，不宣称新性能收益、AMR/多rank/其他精度验收。源、配置、输入、命令、日志、程序、旧程序指纹与检查保留在runs/temporary/uniform_dispatch_removal/round1：[检查](../../runs/temporary/uniform_dispatch_removal/round1/job621977/checks.json)、[源指纹](../../runs/temporary/uniform_dispatch_removal/round1/source_sha256.json)、[安装](../../runs/temporary/uniform_dispatch_removal/round1/production_install.json)。
