# ParaView / C-Viridis GIF 知识收尾

范围：2026-11-04会话中的ParaView显示、坐标指示及C/job624717动画工作；
机械盘点全仓库Markdown，内容审计限于本次实际影响的入口、规则和后处理材料。
不审求解器结构或其他算例，不推断数值正确性或收敛。

## 事实面

| 面 | 状态 | 证据与动作 |
|---|---|---|
| 代码 | verified-current | 长期合成入口含start-frame/frames/crop/duration；旧路径有效软链；渲染驱动仍限本实验40帧 |
| 运行态 | verified-current | client_status实时ready，源码哈希与客户端匹配；正式GIF解码、尺寸/时长/来源SHA256核验通过 |
| 文档 | changed-and-verified | 后处理README将Viridis前15帧设为首选，40帧及旧彩虹版区分；临时脚本索引及仓库导航消除过期用途；补坐标viewport和相机说明 |
| 规则 | verified-current | 根AGENTS.md指向CLAUDE.md；相关规则链无新增覆盖；不需改规则 |
| 记忆 | verified-current | 平台生成记忆只读查找历史边界；generated-read-only，未直接请求改记忆，未写入 |
| 工作区 | verified-current | 单工作树；本次结果纳入版本管理；已有用户场景/稿件/性能记录及图片删除未混入提交 |

## 唯一现役入口

- GIF工具：projects/3Dcases/Computing_performance_test/BOX3D_OSI/data_post_processing/pack_animation_gif.py。
- 当前首选：同目录images/gif-C-viridis-first15/job624717_C_viridis.gif，15帧、t25..375、7.5秒。
- 完整版：images/gif-C-viridis/job624717_C_viridis.gif，40帧、t25..1000、20秒。
- 两者1006×935、500ms/帧，保留坐标与色标；正式源PNG来自frames_final。
- 知识正文在算例后处理README；操作机制在ParaView bridge/publication说明。

## 验证

只读盘点枚举672份项目Markdown；阅读根规则、上级实际存在规则及受影响文档/入口。
相关4份导航/操作文档121个本地链接有效；规则和工具软链有效；Python AST与客户端打包一致性通过。
两份正式GIF及其全部源PNG SHA256匹配来源JSON，逐帧完整解码且时长符合记录。
本轮仅改说明与纳入已验证结果，不重复渲染或重跑求解器。

## 保留及边界

frames_raw、frames_axes_fixed合计约2.7MiB，作为坐标和相机排错证据保留；
frames_final、冻结场景/脚本、逐帧日志、验证JSON及完整GIF是复现材料。
旧合成路径是兼容软链，不是待删临时脚本；不复制第二套算法。
早期直接截取的first15 GIF留作历史，现役为长期工具从原PNG生成的独立first15目录。
没有建议清场项，本轮未删除文件、分支、工作树或停止服务。

pending：无本次范围内待处理项。
out-of-scope：用户已有2DVorticity.pvsm修改、稿件/演示文稿、MLUPS记录及既有图片删除，
其他算例/历史脚本结构审查、SSH服务端根因及多进程最佳数量基准。
warning：全仓库仍有上述用户改动；不能宣称工作树干净。
不涉及推送、PR或部署，只完成本地提交、实时桥接核验和知识收尾。
