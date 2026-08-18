# 后处理脚本使用指南

## 📁 脚本文件清单

整理后的后处理脚本共 **2 个核心脚本**：

### 1. 📊 **data_extraction.py** - AMReX数据提取与导出脚本

**功能：**
- 从AMReX格式的数据文件中提取速度场数据（ux, uy）
- 处理流程：读取 → 计算速度矢量 → 计算涡量 → 裁剪 → 转换为点数据 → 导出CSV
- 支持**单个处理**和**批量处理**两种模式

**使用场景：**
- 单个项目：修改 `PROJECT_FOLDERS` 列表，只保留一个项目名称
- 批量项目：在 `PROJECT_FOLDERS` 列表中添加多个项目名称

**配置参数：**
```python
BASE_PROJECT_DIR = '/path/to/Cylindertest'           # 项目基础目录
BASE_OUTPUT_DIR = '/path/to/output'                  # 输出目录
DATA_SUBFOLDER = 'data/cyl_lev3_Re40_23200'         # 每个项目下的数据子路径
AMR_LEVEL = 2                                        # AMR网格层级
PROJECT_FOLDERS = ['Cylinder2D_BTDF1', ...]         # 项目名称列表
```

**输出：**
- CSV文件，文件名与项目名称相同
- 列：Points:0, Points:1, Points:2, Velocity:0, ..., ux, uy
- 行数：与输入数据一致

**运行方法：**
```python
# 在 ParaView 中
exec(open('data_extraction.py').read())

# 或在命令行
python data_extraction.py
```

---

### 2. 📈 **l2_error_calculator.py** - L2范式误差计算脚本

**功能：**
- 计算两个CSV数据文件之间速度场的L2范式误差
- 支持**单个文件对比较**和**批量文件对比较**两种模式
- 输出绝对误差和相对误差

**使用场景：**
- 单个比较：设置 `AUTO_FIND_FILES = False`，指定具体文件
- 批量比较：设置 `AUTO_FIND_FILES = True`，自动扫描所有CSV文件

**配置参数：**
```python
REFERENCE_FILE = '/path/to/reference.csv'    # 参考解文件
OUTPUT_DIR = '/path/to/output'               # 输出目录
AUTO_FIND_FILES = True                       # 自动查找CSV文件
RESULT_OUTPUT_FILE = '/path/to/errors.csv'  # 结果文件名
```

**计算公式：**

| 指标 | 公式 |
|------|------|
| L2_ux | $\sqrt{\frac{1}{n}\sum(ux_{comp} - ux_{ref})^2}$ |
| L2_uy | $\sqrt{\frac{1}{n}\sum(uy_{comp} - uy_{ref})^2}$ |
| L2_velocity | $\sqrt{\frac{1}{n}\sum[(ux_{comp} - ux_{ref})^2 + (uy_{comp} - uy_{ref})^2]}$ |
| Relative_L2_ux% | $\frac{L2_{ux}}{\|ux_{ref}\|_2} \times 100\%$ |
| Relative_L2_uy% | $\frac{L2_{uy}}{\|uy_{ref}\|_2} \times 100\%$ |
| Relative_L2_velocity% | $\frac{L2_{velocity}}{\|\mathbf{u}_{ref}\|_2} \times 100\%$ |

**输出：**
- CSV表格，包含所有L2误差指标
- 列：File, Points, L2_ux, L2_uy, L2_velocity, Relative_L2_ux%, Relative_L2_uy%, Relative_L2_velocity%

**运行方法：**
```python
# 在 Python 中
python l2_error_calculator.py

# 或作为模块导入
from l2_error_calculator import calculate_l2_error_single
result = calculate_l2_error_single('ref.csv', 'comp.csv')
```

---

## 🚀 完整工作流程

### 步骤 1：提取数据

1. 修改 `data_extraction.py` 配置参数
2. 在 ParaView 中运行脚本或本地运行
3. 获得 CSV 格式的速度场数据

### 步骤 2：计算误差

1. 修改 `l2_error_calculator.py` 配置参数
2. 指定参考文件和比较文件
3. 运行脚本得到误差报告

### 步骤 3：分析结果

- 查看 `l2_errors.csv` 中的误差数据
- 相对误差百分比更能反映解的准确性

---

## 💡 使用建议

### 数据提取

**单个项目提取：**
```python
PROJECT_FOLDERS = ['Cylinder2D_BTDF1']
```

**多个项目批量提取：**
```python
PROJECT_FOLDERS = [
    'Cylinder2D_BTDF1',
    'Cylinder2D_BTDF2',
    'Cylinder2D_BTDF3',
]
```

### 误差计算

**单个比较：**
```python
AUTO_FIND_FILES = False
REFERENCE_FILE = 'reference.csv'
COMPARE_FILES = ['file1.csv', 'file2.csv']
```

**批量自动查找：**
```python
AUTO_FIND_FILES = True
REFERENCE_FILE = 'reference.csv'
# 脚本会自动找出目录中除参考文件外的所有CSV
```

---

## ⚙️ 依赖项

**data_extraction.py：**
- ParaView Python API（需要在 ParaView 中运行）

**l2_error_calculator.py：**
- pandas >= 2.3.3
- numpy >= 2.4.1

安装依赖：
```bash
pip install pandas numpy
```

---

## 📝 输出文件

### data_extraction.py 输出
- 位置：`BASE_OUTPUT_DIR/`
- 格式：CSV
- 命名：项目名称 + `.csv`
- 行数：36366（默认）

### l2_error_calculator.py 输出
- 位置：`RESULT_OUTPUT_FILE`
- 格式：CSV
- 内容：误差汇总表
- 行数：比较文件数量 + 1（表头）

---

## 🔧 常见问题

**Q: 数据提取后没有生成CSV文件？**
A: 检查输出目录是否存在且有写权限。脚本会提示目录不存在。

**Q: L2误差计算结果为 NaN？**
A: 可能参考文件数据为0，导致范数计算出现问题。检查参考文件数据有效性。

**Q: 如何只计算特定的两个文件？**
A: 设置 `AUTO_FIND_FILES = False` 并修改 `COMPARE_FILES` 列表。

---

## 📌 注意事项

1. **文件行数**：所有CSV文件应该有相同的行数（36366行）
2. **数据列**：必须包含 'ux' 和 'uy' 列
3. **参考文件**：用于计算相对误差和作为基准
4. **路径问题**：Windows路径包含空格时要用引号括起

---

## 📞 版本信息

- **创建日期**：2026-01-19
- **上次更新**：2026-01-19
- **脚本数量**：2 个核心脚本
- **状态**：生产就绪
