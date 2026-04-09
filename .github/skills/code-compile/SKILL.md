---
name: code-compile
description: 当需要编译或运行Amrex项目算例时触发，进行正确的编译和运行步骤。
---
# Amrex编译与运行

## 编译步骤
1. 进入具体算例的根目录，例如：`cd projects/Cylindertest/Cylinder2D_IDF`
2. 执行编译命令：`./scripts/compile.sh`

## 运行步骤
1. 在算例根目录执行脚本提交命令：`dsub -s ./scripts/submit.sh`

## 注意事项
- 编译和运行都需要在具体算例的根目录执行
- 编译过程可能需要一些时间，请耐心等待
- 编译完成后，可执行文件会生成在算例目录中
- 结果内容会在.log文件中