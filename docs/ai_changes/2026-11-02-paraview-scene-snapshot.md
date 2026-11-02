# 保存 GUI 实际场景用于远端动画补帧

目的：用户已有七帧 GUI 动画，需要补齐后三十三帧。实际 GUI 已修改色标、边界和标题显示，不能用历史脚本重建近似排版。

范围：桥协议新增只读 snapshot_state、操作实现与分发、版本25生成客户端和说明。使用 ParaView SaveState 写入客户端临时目录并回传 XML，立即清理临时文件；不改变 GUI 对象、相机、时间或数据。不接受任意脚本执行参数。批处理加载该 XML，不改变当前 GUI。

验证：snapshot_state 在真实 Windows GUI 成功，回传活动视图14857、2081×1066与场景XML；已保存到 BOX3D_OSI/runs/a5_B_three_plot3200/job622356/B/figures/animation_completion/gui_scene.pvsm。生成物检查、Python回归56项通过（12项条件跳过）；CLI无参数操作覆盖snapshot_state。

使用记录：保存原七帧SHA256；独立单进程加载完整场景并导出t=175对照图成功。8 rank LoadState路径长时间等待，gdb截取根进程在MPIR_Wait/Allreduce/Reduce调用栈，已停止本次自有MPI测试；不据此推断所有MPI路径均不可用。改用多个独立单进程任务按帧并发，保留对应复现脚本、原始场景、试图、日志及对照数据。
