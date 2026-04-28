# Cylinder2D_IDFtest Python 脚本说明

本目录内的 Python 脚本主要用于分析 `CdCl.dat` 和后处理稳态表面系数文件，例如
`Cf_steady2_step*.dat`。建议在仓库根目录或当前 case 目录下运行；下面示例均从仓库根目录执行。

所有脚本都支持：

```bash
python3 projects/Cylindertest/Cylinder2D_IDFtest/scripts/<script>.py --help
```

查看当前参数说明。

## analyze_cd_convergence.py

用途：从 `CdCl.dat` 中按固定步长抽样，寻找第一次满足 Cd 相对变化小于阈值的时间步。

基本用法：

```bash
python3 projects/Cylindertest/Cylinder2D_IDFtest/scripts/analyze_cd_convergence.py \
  projects/Cylindertest/Cylinder2D_IDFtest/CdCl.dat
```

常用参数：

- `--interval 1000`：抽样间隔，默认每 1000 步检查一次。
- `--rel-tol 1e-3`：相邻抽样点 Cd 相对变化阈值，默认 `1e-3`，即 0.1%。

示例：

```bash
python3 projects/Cylindertest/Cylinder2D_IDFtest/scripts/analyze_cd_convergence.py \
  projects/Cylindertest/Cylinder2D_IDFtest/CdCl.dat \
  --interval 1000 \
  --rel-tol 1e-6
```

输出会打印输入文件、记录数、抽样点数量、判据，以及是否找到收敛点。若未找到收敛点，脚本返回非零退出码。

## smooth_cf_profile.py

用途：对 `Cf_steady2*.dat` 中的 Cf 列做傅里叶低通平滑，保留原始列，并追加 `Cf_smooth` 列。

基本用法：

```bash
python3 projects/Cylindertest/Cylinder2D_IDFtest/scripts/smooth_cf_profile.py \
  projects/Cylindertest/Cylinder2D_IDFtest/Cf_steady2_step523000.dat
```

默认输出文件名是在输入文件名后追加 `_smooth`，例如：

```text
Cf_steady2_step523000_smooth.dat
```

常用参数：

- `--output <path>`：指定输出文件路径。
- `--column Cf`：指定要平滑的列，可以是列名子串或 1-based 列号；默认匹配 `Cf`。
- `--keep-modes 32`：保留的正频傅里叶模态数，默认 `32`。
- `--method split-fourier`：默认方法，分别平滑 `theta < 0` 和 `theta > 0` 两侧，适合当前 `tx>=0` 折叠后的 Cf 定义。
- `--method periodic-fourier`：把整条曲线当作一个周期信号平滑。
- `--preserve-cdv-like`：缩放 `Cf_smooth`，使 `integral Cf_smooth*abs(sin(theta)) dtheta` 与原始值一致。

示例：

```bash
python3 projects/Cylindertest/Cylinder2D_IDFtest/scripts/smooth_cf_profile.py \
  projects/Cylindertest/Cylinder2D_IDFtest/Cf_steady2_step523000.dat \
  --keep-modes 12 \
  --preserve-cdv-like \
  --output projects/Cylindertest/Cylinder2D_IDFtest/Cf_steady2_step523000_smooth_k12.dat
```

输出文件表头会记录平滑方法、保留模态数、积分前后数值等信息。

## smooth_cf_profile_folded_periodic.py

用途：针对当前 `tx>=0` 折叠绘图约定下的 Cf 曲线做周期平滑。脚本先把 `theta < 0` 一侧取反，恢复为带符号周期信号；傅里叶低通后，再把 `theta < 0` 一侧取反折回，最后追加 `Cf_smooth` 列。

基本用法：

```bash
python3 projects/Cylindertest/Cylinder2D_IDFtest/scripts/smooth_cf_profile_folded_periodic.py \
  projects/Cylindertest/Cylinder2D_IDFtest/Cf_steady2_step523000.dat
```

默认输出文件名同样是在输入文件名后追加 `_smooth`。

常用参数：

- `--output <path>`：指定输出文件路径。
- `--column Cf`：指定要平滑的 Cf 列，可以是列名子串或 1-based 列号。
- `--keep-modes 32`：保留的正频傅里叶模态数，默认 `32`。
- `--preserve-cdv-like`：缩放最终折回后的 `Cf_smooth`，保持 `integral Cf_smooth*abs(sin(theta)) dtheta` 与原始值一致。

示例：

```bash
python3 projects/Cylindertest/Cylinder2D_IDFtest/scripts/smooth_cf_profile_folded_periodic.py \
  projects/Cylindertest/Cylinder2D_IDFtest/Cf_steady2_step523000.dat \
  --keep-modes 12 \
  --output projects/Cylindertest/Cylinder2D_IDFtest/Cf_steady2_step523000_folded_periodic_k12.dat
```

如果希望利用折叠曲线背后的周期连续性，优先使用这个脚本；如果希望左右两侧完全独立平滑，使用 `smooth_cf_profile.py --method split-fourier`。

## sort_by_redefined_theta.py

用途：按新的角度定义重新排序 `.dat` 表格，同时保留原始 `theta(rad)` 列。

新角度定义为：

```text
theta_new = MOD(PI() - theta(rad) + PI(), 2*PI()) - PI()
```

基本用法：

```bash
python3 projects/Cylindertest/Cylinder2D_IDFtest/scripts/sort_by_redefined_theta.py \
  projects/Cylindertest/Cylinder2D_IDFtest/Cf_steady2_step523000.dat
```

默认输出文件名是在输入文件名后追加 `_retheta`，例如：

```text
Cf_steady2_step523000_retheta.dat
```

输出表格会把 `theta_new(rad)` 作为第一列，原来的 `theta(rad)` 保留为第二列，其余原始数据列保持不变，并按 `theta_new(rad)` 升序排列。

常用参数：

- `--output <path>`：指定输出文件路径。
- `--theta-column theta(rad)`：指定原始角度列，可以是完整列名、列名子串或 1-based 列号。

示例：

```bash
python3 projects/Cylindertest/Cylinder2D_IDFtest/scripts/sort_by_redefined_theta.py \
  projects/Cylindertest/Cylinder2D_IDFtest/Cf_steady2_step523000_smooth_k12.dat \
  --output projects/Cylindertest/Cylinder2D_IDFtest/Cf_steady2_step523000_smooth_k12_retheta.dat
```

## 常见处理顺序

如果要先平滑 Cf，再按新角度排序，可以按以下顺序执行：

```bash
python3 projects/Cylindertest/Cylinder2D_IDFtest/scripts/smooth_cf_profile_folded_periodic.py \
  projects/Cylindertest/Cylinder2D_IDFtest/Cf_steady2_step523000.dat \
  --keep-modes 12 \
  --output projects/Cylindertest/Cylinder2D_IDFtest/Cf_steady2_step523000_smooth_k12.dat

python3 projects/Cylindertest/Cylinder2D_IDFtest/scripts/sort_by_redefined_theta.py \
  projects/Cylindertest/Cylinder2D_IDFtest/Cf_steady2_step523000_smooth_k12.dat \
  --output projects/Cylindertest/Cylinder2D_IDFtest/Cf_steady2_step523000_smooth_k12_retheta.dat
```
