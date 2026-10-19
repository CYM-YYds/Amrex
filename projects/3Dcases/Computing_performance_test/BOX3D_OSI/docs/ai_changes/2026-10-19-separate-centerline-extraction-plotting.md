# 中心线提取与绘图分离

目的：用户能够独立调整数据来源、参考曲线和坐标范围，重画时无需重新提取。
范围：BOX3D_OSI中心线提取、绘图、相关测试与文档；不改ParaView底层导出协议或求解器。

- 复用已有 `plot_centerline.py`，不增加新脚本。提取入口只保留来源校验、有效采样检查、
  哈希与归一化报告；移除Fig18选择、误差比较、历史曲线叠加与出图。
- 新提取报告命名为 `extraction_report.json`，不覆盖旧 `report.json` 或历史图片。
  新增 `--output-dir` 与 `--lid-velocity`，去掉仅由参考数据选择导致的Re=1000/3200限制。
- 提取复用现有数据读取与归一化函数；Matplotlib延迟至实际绘图时导入。
  两入口不互相启动，CSV为中间接口，无额外转发层。
- 绘图新增ylim、xlabel/ylabel、figsize、linewidth和markersize，保留输入、参考、标题、
  xlim与DPI参数。样式写入绘图JSON，范围变化不裁剪数据或改变误差定义。
- 默认提取不再生成comparison图片、reference_errors或旧比较report；旧结果不动。
  新比较通过独立绘图入口产生。旧历史复现命令对应当时版本，当前两步命令见后处理README。

验证：10项后处理回归测试通过，包括隔离目录复用导出时不依赖参考CSV、不生成图片、
保留旧报告，以及更改图形参数前后归一化CSV和误差完全相同；Python语法、CLI帮助、
文档路径和diff空白检查通过。未调用ParaView或运行CFD，底层导出链未修改。
