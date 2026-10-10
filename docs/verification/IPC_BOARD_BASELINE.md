# 当前 IPC/ROS2 阶段检查点 — 2026-10-10

RTK开发继续搁置。当前阶段为默认关闭的Unix IPC状态/不执行RPM探测，
独立Humble节点已通过设备无访问验证，外部运动接入仍未实现。
用户已完成板端原生Humble ros-base安装；本次核对安装身份并复跑原生DDS回归。
后续补齐Debug/ASan各42/42及四项managed vcan、锁定Humble DDS CI命令、IPC背压/flood计时，
并通过原生板端→host的Fast DDS TCP状态链路、500ms接收过期与2.115s隔离容器网络中断恢复。
同一fixture恢复到新状态为1.011s；原默认DDS与显式UDP失败保留。
2026-10-10同网段复测及网卡抓包/内核日志确认host UFW丢弃新UDP入站（53910/54710）；
独立socket四个主动包到网卡但未送达，既有tuple回复可送达。TCP对照229条状态通过，
六文件逐项与批量主动LSP复查均通过。见[访问排查](evidence/ipc_network_20261010/README.md)。
随后用户授权限定UFW规则：wlan0上board161→host198 UDP54710–54711；实际新增且保留。
显式UDP两实例388条状态、500ms过期及第二实例恢复通过；独立socket四个获准包通过，
相邻54712及默认DDS仍预期阻断。UFW/default deny/原KDE规则保留；仅接受固定profiles的
device-free状态范围。见[UDP复测](evidence/ipc_udp_allow_20261010/README.md)。
113个C++输入与最终交叉产物身份通过，目标链路沿用未改动IPC源的已验证fixture。
完整结果和边界见[补齐证据](evidence/ipc_link_20261009/README.md)。
Debug/sanitizer各4项focused checks及三个IPC vcan场景、锁定Humble DDS均复跑通过；
首次跨CTest并发锁冲突保留并经串行复跑通过。用户手动fixture/topic通过按人工报告归档。
阶段复审与提交范围见[交付复审](evidence/ipc_stage_delivery_20261010/README.md)，
后续顺序见[IPC/ROS实施计划](../plans/ROS2_IPC_NEXT_STEPS.md)。
远端Actions尚未运行本次修改；源状态年龄、跨机probe、物理CAN/UART及运动验收仍独立保留。
运行方法集中于[IPC合同](../development/ROS2_IPC.md)和
[host Docker/RViz与板端原生环境](../../docker/ros2/README.md)。

## Diff 归属

| 范围 | 内容 |
| --- | --- |
| application/ipc、ros2、app、startup | 默认关闭的 Unix SEQPACKET 状态/不执行 RPM 探测；独立 Humble 节点；年龄、序号、范围及断连保护 |
| service/logging、components、依赖文档 | 删除旧 EasyLogger 快照，固定 spdlog v1.17.0；保留 stderr 故障计数、节流、512 字节及完整换行合同 |
| 测试与 CI | IPC 真实 socket/DDS、app vcan 探测场景、日志边界回归、配置和 ELF 检查 |
| platform/linux/can/socket.cpp | 大部分为格式调整；open 先核对 interface index，再创建 CAN socket，改变无接口/无 CAN 支持时的错误优先级 |
| 文档 | 同步当前日志实现、IPC 阶段、历史产物与最新测试缺口，历史失败和验收保持原义 |

本轮小范围整理：spdlog 依赖改为 PRIVATE、去除无用前置声明、formatter 仅在共享 sink
初始化时设置、补齐日志契约；允许既有设备无访问 IPC fixture 交叉构建，CTest 注册仍只在主机。
不回退既有 socket/测试格式改动。`.clangd`、根目录旧 `robot-dev` 包和 `Testing/` 文件保持原位；
初次整理未提交/push，也未重置已有暂存区；本次按用户要求整理为阶段检查点。
提交排除`.clangd`、根目录旧`robot-dev`包及`Testing/`临时文件，均保留原位。

## 原生 Humble 与 host RViz 补充验证

2026-10-09再次核对同一板端machine-id SHA256、aarch64与Linux6.1.84。
清除旧私有运行时的PYTHONPATH/LD_LIBRARY_PATH/AMENT_PREFIX_PATH/COLCON_PREFIX_PATH后，
source /opt/ros/humble/setup.bash，ros2 --help及rclpy/std_msgs导入通过。
系统安装由用户完成，本次不再运行apt或改动系统服务。

| 板端原生包 | 实际版本 |
| --- | --- |
| ros-humble-ros-base | 0.10.0-1jammy.20260910.003535 |
| ros-humble-rclpy | 3.3.22-1jammy.20260907.232408 |
| ros-humble-std-msgs | 4.9.2-1jammy.20260907.211527 |
| ros-humble-rmw-fastrtps-cpp | 6.2.10-1jammy.20260907.212405 |
| ros2-apt-source | 1.3.0~jammy |

先对目标SHA256SUMS逐项校验全部部署文件，再在原生环境执行：

```bash
cd /home/cat/robot-control-tests/ipc-board-20261009T074826Z
# 在未设置nounset的Bash中source官方setup脚本：
source /opt/ros/humble/setup.bash
export ROS_LOCALHOST_ONLY=1 ROS_DOMAIN_ID=187 RMW_IMPLEMENTATION=rmw_fastrtps_cpp
python3 scripts/test/test_ros2_ipc.py ./robot_control_ipc_fixture
```

实际执行设50秒总界限并使用隔离日志目录。shutdown/context竞态与live-context错误传播、
真实DDS状态/探测、SIGTERM、重启、暂停仿真时钟、服务端退出及重连全部通过。
收尾无stage socket或测试进程。首次辅助调用因set -u与官方setup的未定义环境变量不兼容，
在测试开始前退出；原始日志保留，修正调用顺序后通过，不归为节点故障。

Host独立固定Humble desktop镜像已通过真实RViz窗口/OpenGL4.5初始化、配置保存及SIGINT退出；
配置文件属主为host UID。首轮配置路径错误及修正后日志保留。
该轮仅证明GUI smoke；跨机器DDS由上面的后续TCP验证补齐，TF/模型/地图可视化与物理运行未验证。

108个C++编译输入及修复后Python节点/回归测试已与当前工作区逐项比对一致。
可携带的原始身份、JUnit、核心/DDS结果、失败记录及RViz日志见
[evidence/ipc_ros2_20261009](evidence/ipc_ros2_20261009/)。
本地较完整日志仍保留于out/verification/ipc-board-diff与out/verification/ipc-native-handoff。
旧产物、旧失败及已消费HIL执行器不变；原生安装不授予运动或生产部署权限。

## 首次私有运行时实板范围与前提

以下记录保留首次验证时的环境和范围；“板上未安装/opt/ros”由上面的原生安装记录更新。

经现有 SSH alias `robot-dev` 只读核对：Embedfire LubanCat-4IO / aarch64 /
Ubuntu22.04.5 / Linux6.1.84，操作用户 cat。机器身份以本地证据中的 machine-id SHA256核对。
板上未安装 ROS2；`/opt/robot-control/staging` 不可由当前用户写入。

本轮使用用户目录中的独立版本测试目录，保留原部署。先运行配置/帮助、平台 smoke、
纯软件合同和真实 Unix IPC 测试；ROS2 运行时由固定 arm64 官方镜像提取到该目录，
使用进程内环境变量，不安装系统包、不改系统服务。

设备无访问验证不打开真实 CAN/UART、不发电机命令、不写 EEPROM、不复用历史 HIL runner。
生产、运动、真实驱动初始化/恢复、物理待机力矩仍需独立现场条件和有界验收。

## 实际验证

| 层次 | 本轮结果 |
| --- | --- |
| 主机 Debug | 38通过、4项vcan跳过、0失败；debug.xml/debug-tests.log |
| 主机 ASan/UBSan | 38通过、相同4项跳过、0失败；sanitizer.xml/sanitizer-tests.log |
| 主机 Humble DDS | 原始SIGTERM竞态复现失败；新增确定性回归先红后绿，最终DDS通过 |
| 日志/构建脚本 | control-hil diagnostics流、错误sink、Phase1负路径、CI scope及actionlint通过 |
| 静态 | 四个相关TU clang-tidy退出0，保留IPC既有两条位运算提示；Ruff导入、Python语法及diff检查通过 |
| 交叉构建 | 固定GCC11.4/真实Ubuntu22.04 sysroot，8个aarch64 ELF依赖/符号版本/无RPATH审计通过 |
| RK3588软件 smoke | 10个有界检查全部exit0；包括app帮助/配置、IPC配置、设备工具帮助、平台/日志、三组纯合同和真实Unix socket |
| RK3588 Humble DDS | 运行时导入和真实DDS两项exit0；状态/非零不执行探测、SIGTERM、节点重启、暂停仿真时钟、服务端退出及重连通过 |
| 收尾 | 无残留测试进程、无stage socket；系统/opt/ros不存在，原部署保留 |

本轮原始DDS失败是Humble异步SIGTERM使context失效后，executor创建wait set抛出RCLError。
节点仅在context已关闭时完成清理，context仍有效的RCLError继续抛出。
新增回归确定性模拟这两个分支，再执行真实DDS退出/重连；不改变命令或授权合同。
原失败host-dds.log与dds-race-red.log保留，最终dds-race-green.log通过。
两次辅助检查的错误调用（Python执行shell脚本、diagnostics缺少hil参数）已改正，
不归类为产品失败；最终phase1-scripts.log与diagnostics-tests-final.log通过。

四项主机跳过为device_config_vcan_managed、test_app_vcan_managed、canopen_vcan_managed、
socketcan_vcan_managed，原因为namespace capabilities或kernel vcan缺口，不记为通过。
LSP主动扫描的四个C++文件超时，不能声称LSP clean；以编译和clang-tidy为C++依据。
Python导入已在主机和实板真实Humble环境验证，主机缺依赖提示仅局部标注。

## 产物与可复现证据

本地证据：out/verification/ipc-board-diff/。BUILD.json记录交叉配置、镜像、sysroot锁及8个产物hash；
SOURCE_INPUTS.json记录编译TU和项目hpp，实板部署前逐项与工作区比对无差异。
源码快照建立于Python退出修复前，Python不参与这些C++ ELF编译；实际部署的修复后节点/测试
由payload-files.json与目标SHA256SUMS单独记录并逐项校验，不以原快照替代最终Python身份。

目标独立目录：/home/cat/robot-control-tests/ipc-board-20261009T074826Z。
app SHA256：9432add8e2f4dc16c11f0cd22559394e50cca97f26aec0404b19b321ba7683b3。
本地safety_preflight.json记录用户授权、目标身份、包hash、操作范围和恢复路径。
board-results/core.json、dds.json和postflight.json保留每条命令、退出码、耗时及收尾；
每条检查的stdout/stderr分别保留。Unix测试约8.110s，实板DDS约16.791s。

私有arm64运行时来自官方ros:humble-ros-base-jammy平台镜像：
sha256:bbf36a9a05f3df5670a2b98db218e70a1320a956c79f1e0f0a333920049aa605。
从未启动该arm64镜像，只提取Humble、Python依赖、普通共享库和许可证到用户目录；
进程环境指定AMENT_PREFIX_PATH/PYTHONPATH/LD_LIBRARY_PATH及localhost DDS。
目标glibc和loader继续使用系统版本；没有apt/pip安装、系统服务或生产目录变更。
运行时包hash与包版本清单分别保留于safety_preflight.json和runtime/packages.status。
主机原amd64镜像锁不变，不能把两种架构/包版本当作同一镜像。

设备无访问实板软件验证已完成；真实CAN/SBUS、app驱动初始化/恢复与通电零速待机、
机械行为及运动HIL仍未在本轮执行。此结果不授权后续非零命令、EEPROM或生产操作。
原有38通过/4跳过和历史42/42保留，不改写旧失败或已消费runner。
