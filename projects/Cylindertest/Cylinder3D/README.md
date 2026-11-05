# Cylinder3D

三维 LBM + AMR 算例；构建与提交入口见[仓库 README](../../../README.md)。
物理尺度、Re、初始流场和边界仍由 `src/D3Q19.H`、`src/Kernels.H` 中的现有实现确定。

## IBM 运行参数

在 `config/inputs` 中设置，无需重新编译：

```ini
ibm.enabled = true
ibm.geometry = cylinder
ibm.method = mdf
```

| 参数 | 可选值 | 缺省值 |
| --- | --- | --- |
| `ibm.enabled` | `true` / `false` | `true` |
| `ibm.geometry` | `sphere` / `cylinder` | `sphere` |
| `ibm.method` | `idf` / `mdf` | `idf` |

`enabled=false` 时跳过粒子初始化、施力、迁移、受力输出和粒子 checkpoint，
保留供碰撞核读取的零力数组。当前 `state_error_5` 依赖物体位置加密，关闭 IBM
也会关闭物体附近的加密，因此开关两侧并非相同网格的性能对照。
推进递归使用实际生成的最细层。

`cylinder` 复用沿 Y 轴的圆柱侧壁点生成器，不包含端盖，也不自动修改外边界条件。
圆柱高度为 `2*NH*dx_0`，半径为 `rb*dx_min`；AMR 加密范围沿 Y 覆盖整个域。
这不是对无限长圆柱、端部条件或阻力系数的物理验证。
受力文件 `data/CdCl_0.dat` 仍使用已有的球体投影面积归一化，圆柱结果不能直接作为
标准圆柱阻力系数引用；压力采样点也仍为原球体实现，当前主程序未执行压力采样输出。

`idf` 复用稠密矩阵直接施力，三维点数增大时开销较大；`mdf` 复用直接施力迭代，
迭代次数仍由 `src/D3Q19.H` 的 `NF` 决定（当前为 1）。本次未引入编译开关，
避免相同功能由两套配置共同决定。

运行时参数可通过命令行覆盖：

```bash
./main3d.gnu.MPI.CUDA.ex config/inputs ibm.enabled=false
./main3d.gnu.MPI.CUDA.ex config/inputs ibm.enabled=true ibm.geometry=cylinder ibm.method=mdf
```

提交入口默认使用本算例的 `main3d.gnu.MPI.CUDA.ex`，可通过
`AMREX_EXECUTABLE` 覆盖；当前资源账户为 `root.iosoeqkp`。
运行参数通过提交系统显式传入：

```bash
dsub -x 'all;AMREX_RUN_ARGS=ibm.enabled=true ibm.geometry=cylinder ibm.method=mdf' -s ./scripts/submit.sh
```

## 重启与验证

新 checkpoint 增加 `IBMConfig` 文件，记录开关、几何、方法和是否包含粒子。
重启要求开关一致；IBM 开启时还要求几何、方法一致且保存了粒子状态。
旧 checkpoint 没有此文件时按原默认球体 IDF 解释。重启不会把带物体流场自动转换为无物体流场。

本次验证输入、命令、程序、日志和场文件保留在 `runs/temporary/ibm_runtime_switch/`；
复现入口为 `scripts/temporary/ibm_runtime_switch/submit.sh`。
该目录属于临时验证实验，但保留其中输入、日志、报告及关键数值材料；
完整验证结果见 `docs/ai_changes/2026-11-05-ibm-runtime-switch.md`。
