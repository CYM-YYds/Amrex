# 几何路径构建平均缓存

- 目的：移除 `BuildAverageCache()` 中仅用于 Box 分解的 GPU mask 回拷，避免设备到主机数据传输。
- 文件：`src/AmrCoreLBM.cpp`。
- 改动：使用 coarse 化 fine `BoxArray`、周期感知 `complementIn`、`grow/intersect` 和 `removeOverlap` 直接生成 interface Box，并建立 owner 与 fine Fab 索引。搜索 Box 不裁剪到物理域，以包含非周期物理边界外的一圈未覆盖 ghost，与 `interface_mask` 的判定一致。
- 验证：`./scripts/compile.sh` 编译成功，生成 `main3d.gnu.TPROF.MPI.CUDA.ex`。
