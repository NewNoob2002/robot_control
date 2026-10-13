# ROS2 与 app 的 IPC 第一阶段（当前检查点：2026-10-10）

基于 b6cf527，开发分支 codex/ros2-control-thread-integration。
用户恢复此前暂停的IPC工作；本阶段落实移植计划中“先读状态、验证退出，再接运动”的顺序。
已实现独立Humble节点、app状态回传和**不执行的RPM探测**。CycleInput.external仍为空，
当前唯一运动来源仍是SBUS；连接成功、probe_ok、healthy、armed均不表示IPC获准运动。

## 为什么使用 socket

采用本机 AF_UNIX / SOCK_SEQPACKET：独立进程可按路径连接，双向通信并保留包边界，
无需为字节流额外维护分帧缓存。与匿名pipe的父子进程/继承FD模型相比，更适合ROS节点
独立启动与重启；FIFO则需要额外处理双向通道、连接身份和消息边界。首版不需要TCP、
共享内存、额外库或把ROS executor放进硬件进程。此处为app↔节点的本机IPC；跨机DDS传输独立配置。

~~~text
ROS2 ~/probe → 节点校验/单槽 → Unix SEQPACKET → app owner每周期最多1包
ROS2 ~/status ← 状态JSON    ← 校验结果+控制快照 ← ControlLoop::step之后
                                                   ↓
                                     无命令提交；不填充external
~~~

控制/CAN/UART/退出归属保持在app。IPC只读取值副本，不保存控制对象引用，不新增线程，
不触发SDO、NMT、RPDO或使能。IPC使用sendmsg/recvmsg，CAN的send/recv链接门控保持原样。

## 配置与运行

ipc_socket=off是默认值，旧配置可省略。开启时必须显式指定小于108字节的绝对路径，
父目录必须是当前UID所有的真实目录，权限0700；socket权限0600，仅接受同UID进程。
每次最多保留一个客户端，第二个连接会被关闭。app不会删除启动前已有的路径，
退出仅删除自己创建的socket inode。崩溃留下的旧socket须确认无存活实例后手动清理。

以下只展示配置方式，启动app仍需要相应设备操作授权：

~~~sh
mkdir -m 700 /tmp/robot-control-ipc
# 在原有已审核的app启动参数后追加：
# --set ipc_socket=/tmp/robot-control-ipc/control.sock

# ROS2 Humble环境内；与app共用socket目录及同一内核CLOCK_MONOTONIC时钟域
python3 ros2/robot_control_ipc_node.py --ros-args \
  -p socket_path:=/tmp/robot-control-ipc/control.sock
ros2 topic echo /robot_control_ipc/status --qos-reliability best_effort
~~~

节点由根CMake随app保留顶层 robot_control_ipc_node.py，同时安装最小ROS包
`robot_control_ros2`；`ros2/`也可独立由colcon构建。提供`ros2 run`入口与
`diagnostics.launch.py`（bridge/monitor二选一），核心app仍不链接ROS库。
安装、启动和本机接收过期显示见[诊断包使用说明](ROS2_DIAGNOSTICS.md)。
第一阶段主机环境复用docker/ros2/image.lock锁定的Ubuntu22.04/Humble镜像，
其中已有rclpy和std_msgs（均为Apache-2.0），核心app不链接ROS库。
最初RK3588设备无访问验证使用用户目录提取的固定arm64官方Humble运行时。
2026-10-09用户完成原生Humble ros-base安装，/opt/ros/humble的导入与相同DDS回归已通过。
具体版本及通过范围见[实板基线](../verification/IPC_BOARD_BASELINE.md)；
原生板端与host Docker/RViz用法集中于[环境说明](../../docker/ros2/README.md)。

节点启动参数socket_path必填；运行时不支持切换endpoint，应重启节点。
~/status和~/probe使用std_msgs/msg/String承载本阶段诊断JSON，QoS为
BEST_EFFORT、VOLATILE、depth1。它们不是最终运动命令消息，也不接受/cmd_vel。
探测JSON只含三个整数：

~~~json
{"captured_ns":123456789000,"left_rpm":5,"right_rpm":-5}
~~~

示例时间是占位值，发送者必须在采样时使用本机time.monotonic_ns()填写；
不得用ROS仿真时间或Unix墙钟，也不得由转发回调刷新旧时间。客户端检查整数类型、
源时间与协议最大范围，服务端再检查app配置的max_rpm。节点最多保存一个待发探测，
只有一个在途请求；节点未连接时拒绝探测，断连丢弃探测，重连不重发旧值。

## 第一版线协议

每个请求是一个无换行ASCII包，最多192字节，字段恰好用一个空格分隔：

~~~text
RCI1 sequence captured_ns kind left_rpm right_rpm
~~~

| 字段 | 合同 |
| --- | --- |
| sequence | uint64，连接内从正数开始严格递增；新连接重置 |
| captured_ns | Linux CLOCK_MONOTONIC纳秒，正int64；0≤年龄<100ms |
| kind | 0查询状态（双RPM必须0），1校验探测（不执行） |
| left/right_rpm | 有符号整数，逻辑左右轮方向；不在ROS端重复翻转驱动方向 |

结构错误、溢出、多余字段/空白、版本不符、序号重放、未来/过期、RPM越界分别被拒绝。
回复是最多1024字节JSON。result为status/probe_ok/malformed/sequence_replay/
stale_or_future/rpm_range之一，executed=false与motion_authority=false始终不变。
序号有效的过期/超限请求也消耗其序号，不能用同一序号重新提交。

回复包含：server_started_ns、连接session、请求sequence、状态采集captured_ns、
cycles、source（0无/1SBUS/2external）、左右目标rpm、左右反馈0.1rpm、status_raw、
fault_raw、healthy及armed。原始状态值是JSON整数，不改变CAN/OD日志的十六进制格式。
这些字段仅供诊断，尤其armed不等于当前允许非零输出。

每周期最多accept一次和recvmsg一次；无等待、无接收循环、无每请求堆分配。
截断包/附带控制数据/EOF断开；无请求1秒断开；回复背压或发送错误立即断开。
节点使用显式STEADY_TIME时钟每20ms轮询；use_sim_time=true且无/clock或仿真暂停时，
IPC轮询、应答超时和重连仍推进。发出请求后的下一次tick接收，通常约25Hz状态；100ms应答超时后
发布connected=false，500ms后尝试重连。错误回复不会伪装成成功或命令执行确认。
状态消费者必须自行检查时间/断连，不能无限沿用最后一条healthy=true。

SO_PEERCRED只限定本机同UID信任边界，不是对同UID恶意进程的隔离。
此协议的session和server_started_ns仅用于诊断，**不是可复用的运动授权合同**；
未实现跨重启挑战、boot identity、时间命名空间证明或外部授权代际。

## 可重现的无设备验证

~~~sh
cmake -S . -B out/build/test-app-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DROBOT_CONTROL_BUILD_TEST_APP=ON
cmake --build out/build/test-app-debug -j 4
ctest --test-dir out/build/test-app-debug --output-on-failure

# 已source Humble的环境中，以根CMake编译的同ABI fixture为参数：
python3 scripts/test/test_ros2_ipc.py out/build/ros2-ipc-humble/app/robot_control_ipc_fixture
~~~

app_ipc_contract覆盖真实socket的字段/年龄/序号校验、既有路径保护、第二客户端、
重连、背压、空闲清退、清理以及Python客户端。
test_app_vcan_managed增加ipc_probe/ipc_drive：验证非零探测无运动，观察者滞留时
SBUS仍连续输出，真实CAN发送门控与退出恢复继续工作。全部在独立vcan/PTY内运行。
ROS测试实际通过DDS发布探测/读取状态，验证SIGTERM、节点重启、服务端退出和新实例重连；
另覆盖context在executor检查与wait-set创建之间关闭的竞态，仍有效的context错误不得被吞掉；
重启节点启用use_sim_time且不发布/clock，验证暂停仿真时这些IPC行为仍正常。
ROS2回归可通过`bash scripts/test/test_ros2_ipc.sh`在锁定镜像中重现；镜像按
`docker/ros2/README.md`预先拉取。CI的`Humble IPC DDS`任务对runtime/full范围运行，
并加入汇总门控；实际覆盖退出竞态、状态/探测、暂停仿真时钟和断连恢复。

当前补齐验证：Debug和ASan/UBSan各42/42通过，四项managed vcan均实际执行且无CTest跳过；
`ipc_probe/ipc_drive/ipc_flood`覆盖背压、每批32包的持续请求、非执行合同及连续SBUS输出。
停止诊断新增`owner_cycles/owner_max_work_us/ipc_max_poll_us/owner_overruns`；
owner工作时间从计划CAN等待结束计至日志记录和IPC完成，不计CAN等待阶段或清理。
Debug三个场景owner最大306–349us、sanitizer为777–890us，均无10ms预算超限；
这是主机隔离vcan测量，不是RK3588实时性或硬件验收。锁定交叉构建/ELF与113个C++输入身份通过。

RK3588原生Humble到host的诊断状态已通过进程级Fast DDS TCP配置：三次fixture实例、
500ms接收静默过期、2.115s隔离容器网络中断及同一服务实例恢复通过；重连到新状态1.011s。
默认DDS和显式UDP peers此前在路由网络未收到数据；原始失败保留。
2026-10-10同网段/更新地址复测仍失败，网卡抓包、内核UFW BLOCK和独立socket对照
确认host丢弃新的UDP入站。TCP通过；六文件逐项/批量LSP检查均通过，旧超时不再复现。
见[访问排查](../verification/evidence/ipc_network_20261010/README.md)。
随后用户授权并新增wlan0/board161→host198/UDP54710–54711限定规则，原规则/default deny保留。
固定显式UDP profiles复测388条状态、500ms过期及新fixture恢复通过；独立socket四包送达，
相邻54712和默认DDS仍预期阻断。必须为进程显式选择所存host/board profiles，固定端口不能
未经规划供多个参与者共用。规则和回退见[UDP复测](../verification/evidence/ipc_udp_allow_20261010/README.md)。
消费者只用host接收时间判新鲜度，未证明源状态年龄，不比较跨机CLOCK_MONOTONIC。
跨机probe仍不符合本机时间域合同；真实CAN/UART和驱动HIL未执行。
完整命令、未执行项、身份及失败见[补齐证据](../verification/evidence/ipc_link_20261009/README.md)。
新CI任务已本地执行等价命令，尚无本次源码的远端Actions运行。

此前spdlog/IPC时钟修复的38通过/4跳过记录保留在[验证基线](../verification/TEST_APP_BASELINE.md)，
旧RK3588本机IPC/DDS和RViz smoke见[实板基线](../verification/IPC_BOARD_BASELINE.md)。
fixture以BUILD_TESTING=ON交叉构建，目标端手动执行Python检查，不在主机CTest执行aarch64 ELF。

当前阶段复审及验证身份见[交付复审](../verification/evidence/ipc_stage_delivery_20261010/README.md)，
手动启动与socket生命周期见[手动复测](IPC_MANUAL_TEST.md)。用户报告fixture/topic测试通过，
作为人工观察归档，不代替自动过期/恢复或真实设备验收。
后续顺序和完成门槛见[实施计划](../plans/ROS2_IPC_NEXT_STEPS.md)。

## 后续运动接入阶段

详见[状态年龄和外部运动授权合同](../plans/ROS2_EXTERNAL_AUTHORITY_CONTRACT.md)：
当前诊断receipt age已实现，source age未知；运动v2挑战/采样证据/授权代际和撤权合同
已定义，尚未签发或接入external。`probe_ok`不能作为运动确认。

1. 实现服务端主导的启动/连接挑战、授权代际、显式切源和中立重授权合同；
   队列积压、节点死亡和跨进程旧时间均撤权，不能把本阶段probe改名就直接驱动。
2. 以既有ControlArbiter/ControlCycle的external入口实现，只允许owner提交；
   保留SBUS联锁/人工接管、零保持、代际复核、反馈守卫和有界停车。先加离线/vcan回归。
3. 明确算法输入单位；如需Twist，先给定实测轮径、轮距、减速比，再实现转换。
4. 软件门槛后再审查真实驱动/物理验证；当前仅独立用户目录的设备无访问软件部署与验收，
   没有EEPROM变更、真实驱动HIL或运动验收。
