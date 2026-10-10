# IPC / ROS topic 阶段复审与交付 — 2026-10-10

**复审结论：PASS_WITH_DOCUMENTED_LIMITS。当前只读诊断范围可提交归档，未发现阻止该范围交付的缺陷。**
复审由当前assistant直接执行，未委派或声称独立审查。基准commit为
`3823e51b77a93b8f94f14d2bc0bff6496a278d16`；当前源身份由FILES.json固定。
本文件随交付commit保存，不把构建前的dirty revision误记为本次commit。

## 已复审行为

| 路径 | 结论与证据 |
| --- | --- |
| Unix IPC | AF_UNIX/SOCK_SEQPACKET、非阻塞、同UID、owned0700目录/socket0600；每poll最多一次accept/recv/send，背压即清退；已有路径不覆盖，析构只删自己的inode |
| 请求校验 | 固定字段/整数范围、严格序号、100ms本机CLOCK_MONOTONIC有效期、状态查询零RPM和probe范围；executed/motion_authority始终false |
| ROS节点 | 独立rclpy executor，BEST_EFFORT/VOLATILE/depth1，steady timer20ms；单槽probe、单在途请求、100ms超时/500ms重连；断连清除probe，SIGTERM/context竞态有回归 |
| 控制所有权 | app在loop publication后给IPC复制快照；CycleInput.external仍为空；ROS/IPC不发CAN、不改驱动、不授予运动 |
| 计时改动 | 从计划CAN事件等待结束到cycle记录/IPC完成；不计等待前dispatch或shutdown，无策略依赖；停止输出owner/IPС统计 |
| CI | 新增锁定Humble真实DDS检查，runtime/full选择并进入aggregate gate，routing/actionlint及本地等价命令通过；远端CI未运行本次变更 |
| 日志声明迁移 | 既有旧头删除保留，声明迁至stderr_sink_status.h，C ABI符号不变，两个include与当前构建/测试吻合 |
| 网络 | 已归档TCP状态与同实例网络恢复、授权限定UDP状态/过期/新实例恢复；固定UDP只覆盖一对参与者，默认DDS未放行 |

## 当前验证与保留的失败

- 既有完整Debug/sanitizer各42/42及managed vcan四项、锁定cross/ELF、113个C++输入身份
  见[补齐验证](../ipc_link_20261009/README.md)。本次重核15个verification源文件及113个
  C++输入SHA256均匹配；无新增源码行为修改，因此沿用该交叉/静态结果。
- 本次两种build重新构建成功。各4/4 focused CTest通过：app_ipc_contract、
  startup_config_contract、control_cycle_contract、linux_platform_integration。
- 当前Debug和串行sanitizer的managed vcan均实际运行ipc_probe/ipc_drive/ipc_flood三场景，
  目标/恢复/nonexecution断言通过；不把只选三场景报为重跑全部42项。
- 首次sanitizer vcan启动失败：parent同时跑两个独立CTest进程，二者争用同一app接口锁，
  错误明确为`app lock unavailable: Resource temporarily unavailable`，在控制启动前退出。
  同一CTest内RESOURCE_LOCK不会跨CTest进程协调。并发结束后串行复测通过；原失败log/JUnit保留。
  未改锁策略、跳过检查或放宽断言；后续相关测试须在共享工作区串行执行。
- 锁定Humble真实DDS回归重新通过：状态/probe、SIGTERM、node restart、paused simulation
  clock、server loss/reconnection及live-context错误不隐藏。
- CI routing、actionlint、Bash syntax、Ruff及变更C++格式检查通过。shellcheck仍不可用，
  历史clang-tidy六条警告与原LSP超时保留；后续已主动复查六文件无超时/零诊断。
- 旧host-udp.xml在手动操作期间被重排空白，原checksum不匹配；canonical XML比较语义
  相同。格式化副本保留为host-udp-manual-formatted.xml，历史归档路径恢复当时验证原字节。
  ARCHIVE_INTEGRITY.json记录两个hash与处理原因，三个原证据库的179个checksum重新匹配。
- 用户聊天明确报告手动fixture/topic测试通过，未提供原始日志或精确时序；
  USER_MANUAL_REPORT.json记录为operator-reported通信功能，不据此新增500ms/HIL验收。
  新[手动说明](../../../development/IPC_MANUAL_TEST.md)包含加载Humble、系统Python、
  容器续行、topic订阅、8秒fixture/socket生命周期及固定端口注意事项。

## 已归档范围与后续限制

[访问诊断](../ipc_network_20261010/README.md)保留UFW阻断和原未应用提案；
[授权UDP复测](../ipc_udp_allow_20261010/README.md)独立记录实际规则新增和388样本。
规则保持active；它是用户授权的本机状态，不是commit自动应用的系统配置。
本次复审不重启板端现场手动节点、不重改防火墙、不访问物理CAN/UART/驱动。

状态交付freshness只按host接收时间，未验证source-state age，禁止跨主机monotonic相减。
probe只适用board-local时钟域。session/server_started_ns为诊断，不是运动授权挑战。
fixture反馈为合成值；真实app新入口、rpm尺度、零速保持力矩/机械制动、物理运动、
CAN延迟TX残余与延期单侧故障仍独立。瞬时主机owner计时不是RK3588实时验收。
负载测试是有限批次/背压清退后的控制持续性，不声明任意攻击负载或长时性能保证。

后续按[实施计划](../../../plans/ROS2_IPC_NEXT_STEPS.md)顺序推进：最小ROS包/launch与过期显示，
状态年龄/授权合同，已有external入口的离线/vcan接入，另行授权真实零目标及有界运动。
RTK继续暂停。当前用户授权归档和本地commit，不包含push或生产部署。

## 提交范围

提交当前IPC计时/集成测试、Humble CI、logger声明迁移、阶段文档及四个证据目录。
`.clangd`为已有用户改动，保持未暂存；其他无关临时out文件不入commit。
本次归档原始失败、Junit、PCAP/JSONL及构建身份不删除、不改判为通过。
SHA256SUMS固定此阶段档案；RESULT.json列出重跑结果与对应旧验证。
