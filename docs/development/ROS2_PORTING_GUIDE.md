# 整车控制框架与 ROS2 移植交接说明

面向算法/ROS2集成同事，依据2026-09-21工作区源码整理。
本文说明现有实现和建议移植边界；ROS2节点、消息和服务均为建议，当前尚未实现ROS2接入，
IPC客户端暂停。2026-09-21更新：speed_resolution已拆为独立设备配置/保存与app只读校验；本文不代表硬件验证。
运行与调参另见 [TEST_APP.md](TEST_APP.md)。
当前app开启健康驱动器通电零速待机：没有行驶授权也可保持零目标伺服，不能把CH6关闭理解为断电。
待机/未命令轮反馈故障阈值为±5rpm；重新授权和停车的静止确认仍为±2rpm。
ROS2迁移须保留这两个不同判据与单一控制者，不能以topic发布或节点存活直接授予运动权限。

## 1. 控制归属与整体结构

当前入口 [app/main.cpp](../../app/main.cpp) 组合Linux串口、CANopen、SBUS Source、
单一ControlLoop和独立日志进程。CAN事件处理、UART批量读取、仲裁、安全决策和最终发送
由同一个控制线程顺序执行，不是四个现成独立节点。

推荐先包装成一个ROS2硬件控制节点，内部保留普通C++控制线程，再按需要分离输入生产者。
节点拆分必须保留以下合同：

- 只有一个执行者拥有CANopen生命周期、驱动状态机和最终发送权限。
- ROS2回调只提交带时间/代际的命令快照，不直接发CAN、写SDO或使能电机。
- ROS2停止、阻塞、命令过期时，控制执行者仍自行检测超时并停车。
- CANopen栈自己拥有SocketCAN端点；CAN与CANopen不是可直接用topic连接的两个现成模块。
- 仲裁、安全决策及实际发送确认保持明确顺序，不分散到互不协调的回调。
- StackStorage限制同一进程只有一个栈实例；生成OD有可变全局数据，不能创建多个组件实例共用它。

~~~mermaid
flowchart TD
    UART[SBUS串口] --> Reader[Reader与Parser]
    Reader --> Source[Source 健康/CH6/CH7/差速混合]
    Source --> Sample[CommandSample]
    ROS[未来算法命令适配器] --> External[external快照]
    Sample --> Cycle[ControlCycle 仲裁与安全]
    External --> Cycle
    Cycle --> Runtime[RuntimeSession与RuntimePolicy]
    Runtime --> RPDO[SocketCAN RPDO]
    RPDO --> Drive[电机驱动器]
    Drive --> RX[Lifecycle CAN接收]
    RX --> Obs[ObservationStore]
    Obs --> Runtime
    Runtime --> Cycle
    Setup[ControlQualification SDO/NMT初始化恢复] --> Drive
~~~

## 2. 源码导航

| 职责 | 文件 | 首先阅读的符号 |
| --- | --- | --- |
| 启动、运行与停止编排 | [app/main.cpp](../../app/main.cpp) | StackConfig、setup.prepare、Loop::create、step、stop、finish |
| 启动配置与固定映射 | [application/config/startup.cpp](../../application/config/startup.cpp) | parse、dump |
| UART机制 | [platform/linux/uart/serial_port.cpp](../../platform/linux/uart/serial_port.cpp) | SerialPort实现 |
| SBUS纯协议 | [input/sbus/protocol/parser.cpp](../../input/sbus/protocol/parser.cpp) | Parser::consume、decode、resynchronize |
| 串口批次与断流 | [input/sbus/linux/reader.cpp](../../input/sbus/linux/reader.cpp) | Reader::open、read、reset_stream |
| Reader→Source | [input/sbus/linux/source_bridge.cpp](../../input/sbus/linux/source_bridge.cpp) | update_source |
| 通道/健康/授权/速度映射 | [input/sbus/source/source.cpp](../../input/sbus/source/source.cpp) | Source::consume、accept_frame、tick、snapshot |
| 周期执行顺序 | [application/control/linux_loop.cpp](../../application/control/linux_loop.cpp) | ControlLoop::create、step、stop |
| 仲裁、安全与状态转换编排 | [application/control/control_cycle.cpp](../../application/control/control_cycle.cpp) | ControlCycle::prepare、complete |
| 命令选择/切换/过期 | [domain/control/control_arbiter.cpp](../../domain/control/control_arbiter.cpp) | ControlArbiter::evaluate |
| 机器人安全状态 | [domain/safety/safety_manager.cpp](../../domain/safety/safety_manager.cpp) | SafetyManager::evaluate |
| 运行守卫与负载编码 | [domain/drive/runtime.cpp](../../domain/drive/runtime.cpp) | RuntimePolicy实现 |
| 驱动协议 | [domain/drive/cia402.cpp](../../domain/drive/cia402.cpp)、[zlac8015d.cpp](../../domain/drive/zlac8015d.cpp) | 状态/目标/反馈编解码 |
| CANopen生命周期 | [communication/canopen/lifecycle.cpp](../../communication/canopen/lifecycle.cpp) | create、run_until、process_receive_event |
| 接收观测快照 | [communication/canopen/observation.cpp](../../communication/canopen/observation.cpp) | ObservationStore::ingest、snapshot |
| 最终输出/每事件抑制 | [communication/canopen/runtime.cpp](../../communication/canopen/runtime.cpp) | RuntimeSession::create、refresh、submit、send、stop |
| 初始化与恢复 | [communication/canopen/control_qualification.cpp](../../communication/canopen/control_qualification.cpp) | prepare、check_baseline、mapping、finish |
| SDO/NMT事务 | [communication/canopen/qualification.cpp](../../communication/canopen/qualification.cpp) | upload、download、verify_download、configure_rpdo_mapping |
| 本地OD配置 | [communication/canopen/stack_storage.cpp](../../communication/canopen/stack_storage.cpp) | apply_receive_configuration、configure_active_counts |
| app额外保护/记录 | [app/io_gate.cpp](../../app/io_gate.cpp)、[motion_monitor.hpp](../../app/motion_monitor.hpp) | send/recv包装、MotionMonitor |
| 持续日志 | [app/runtime_log.cpp](../../app/runtime_log.cpp)、[record.hpp](../../app/record.hpp) | 独立收集进程、轮转、记录格式 |

## 3. SBUS：从串口到左右轮目标

~~~text
SerialPort → Reader::read → Parser::consume
           → ReadBatch(events/raw/session/captured_at/discontinuity)
           → update_source → Source::consume → Source::accept_frame
           → SourceSnapshot.sample（CommandSample）
~~~

Parser处理25字节帧、16通道与flags，不依赖Linux、不做运动决策。
Reader负责100000波特率/8E2/PARMRK、部分帧与批次预算、服务间隙和会话变化。
只复制Parser会丢失断流、错误与批次有效性处理。

Source把CH1转向、CH3油门按标定归一化到[-1000,1000]并施加摇杆死区。
CH6按释放后的新按下边沿切换使能，重新使能要求中立；释放本身不是禁能。
CH7选择gear_rpm，数组索引为零基，CH7对应索引6。

Source::accept_frame的差速混合：

~~~text
左逻辑目标 = mix(throttle + steering, gear_rpm, config)
右逻辑目标 = mix(throttle - steering, gear_rpm, config)
mix：输入夹到±1000 → 乘档位rpm/1000（整数）
     → maximum_rpm限幅 → 小于output_deadband_rpm时置零
~~~

逻辑方向在RuntimeConfig中转换到驱动方向，不要在ROS2节点重复反向。
2026-09-21：startup.cpp注入30%/60%/100%三档，整数向下取整且配置值至少1rpm。
整车app在startup.cpp注入1rpm输出死区，等于1rpm保留；max_rpm=25时为7/15/25rpm，
max_rpm=5时为1/3/5rpm。Source库默认仍为3rpm，移植时需传入app的配置才能保持一致。
生效值在--check-config/config.conf中记录，参见TEST_APP.md。
帧丢失/failsafe、过期、回放、会话变化、数据不连贯等都会撤销授权。
独立SBUS节点必须在无数据时调用Source::tick检测静默过期，不能只在收到帧时检查。

## 4. 仲裁、安全与最终输出

ControlLoop::step的关键顺序：

1. Lifecycle::run_until处理到下个控制时刻的CAN事件和截止时间。
2. Reader读取有界批次，update_source更新Source，并覆盖CycleInput.sbus。
3. 执行命令资格检查和app的stop_check。
4. ControlCycle::prepare(input,runtime.state(),now)完成仲裁、安全和CiA402意图。
5. RuntimeSession::submit对当前会话请求进行复核并发送RPDO。
6. ControlCycle::complete确认实际守卫结果；失败进入停止路径。

默认周期10ms，记录遗漏周期和执行延迟，不保证硬实时。
RuntimeSession::refresh在CAN接收事件及超时处理中更新守卫，撤权可直接产生零输出，
不能移植成只等待低频反馈topic才判定故障。

[CommandSample](../../domain/command/command_sample.hpp)包含左右rpm、stop_requested、
captured_at、session_generation、authorization_generation、sequence及健康标志。
ControlCycle还需明确emergency_stop_known/active，未知急停不能默认未触发。
机器人安全状态和CiA402驱动状态是两套状态机。

当前app没有填充CycleInput.external，实际仅SBUS。策略层虽有external入口，
仍需SBUS联锁、中立/零保持、新授权及代际检查，接上/cmd_vel并不等于完成控制权切换。

## 5. CAN、CANopen与两类OD

### 实际收发

[platform/linux/can/socket.cpp](../../platform/linux/can/socket.cpp)提供通用CAN机制，
但app的CANopen端点由CANopenLinux管理，不能假定所有帧都经过该通用Socket类。

- 接收：Lifecycle epoll → recv(MSG_PEEK) → CO_CANrxFromEpoll消费
  → 验证peek/consume一致 → ObservationStore::ingest → RuntimeSession::refresh。
- 初始化SDO/NMT：QualificationSession → CANopenNode SDO状态机/受限NMT发送
  → CANopenLinux CO_driver.c → SocketCAN。
- 周期目标：RuntimeSession::submit → RuntimePolicy → RuntimeSession::send，
  在同一CAN端点调用send(MSG_DONTWAIT)。RPDO为0x200+node、DLC6：
  两字节控制字与四字节双轮目标。内核接受发送不证明驱动已执行。
- app以GNU链接器--wrap=send/recv附加门控和记录，见app/CMakeLists.txt与io_gate.cpp。
  只复制main.cpp会遗漏这部分行为。

node1的SDO请求/应答为0x601/0x581、心跳0x701、
TPDO1 0x181（双状态/双速度）、TPDO2 0x281（模式/故障）、RPDO1 0x201。
长度、时间、transport/boot代际与原始值一起参与有效性判断。
CAN波特率、restart-ms和link-up由系统配置，app只选用现有接口。

### 本地OD与远端OD

[od/OD.c](../../communication/canopen/od/OD.c)、
[od/OD.h](../../communication/canopen/od/OD.h)是生成的本地主站字典；
stack_storage.cpp配置本地SDO客户端、心跳消费者和接收PDO对象。
电机TPDO在本地主站是接收PDO，命名方向不同。
当前生成文件保留原样，出处见 [PROVENANCE.md](../../communication/canopen/od/PROVENANCE.md)。

电机内部OD通过SDO访问。修改本地OD不会自动修改电机0x2026，
访问远端厂商对象通常也不需要在本地主站加入同名对象。

### 初始化与退出

app创建Lifecycle后调用ControlQualification::prepare(true,20)：
检查当前在线状态、模式/故障、零目标、±2rpm静止带和固定PDO基线；
配置易失通信参数/映射并取得当前代际RuntimeLayoutProof，再创建ControlLoop/RuntimeSession。
不要求历史boot-up，不无条件覆盖任意初始状态。

运行期主要使用TPDO/心跳/EMCY及RPDO，没有通用在线任意SDO服务。
ControlQualification::current在RuntimeSession附着时返回EBUSY。
退出先loop->stop、检查静止、释放loop/runtime，再setup.finish执行SDO/NMT恢复；
同时保留主失败和清理失败。析构/ROS2进程退出不能代替停车和恢复。

0x1017写入的500是raw值。已有指定驱动证据为0.5ms/count，
不要根据变量名将它误称为实际500ms；其它固件需核对。
参见 [驱动说明](../ZLAC8015D_CANOPEN_NOTES.md)。

## 6. 增加0x2026:05：应该调用哪个API

在现有初始化类内部复用ControlQualification::read/expect/write；
write调用QualificationSession::verify_download完成SDO写入和读回比对。
这些是private接口，不存在main或ROS2回调可直接调用的公共write_od。
[sdo_client.hpp](../../communication/canopen/sdo_client.hpp)只有关闭/清理辅助函数，不是通用读写接口。

| 内部API | 用途 |
| --- | --- |
| ControlQualification::read(index,sub) | 按允许宽度上传，返回Result<uint32_t>，记录raw |
| ControlQualification::expect(index,sub,value) | 读取并比较raw |
| ControlQualification::write(index,sub,value) | 按允许宽度编码小端值，写入并读回 |
| QualificationSession::upload(index,sub,require_fresh,deadline) | 有界expedited SDO读，返回SdoObservation |
| QualificationSession::download(index,sub,bytes,require_fresh,deadline) | 有界expedited SDO写，支持1..4字节 |
| QualificationSession::verify_download(...) | 写入后上传指定对象并精确比较 |

两个阶段共用.conf的驱动器分区，字段和范围定义在communication/canopen/device_parameters.hpp，
配置解析器验证范围、I/O双字节及软件max_rpm不超过明确填写的drive_max_rpm。

- robot-control-device-config调用configure_device(config.drive)，只在禁用/静止且心跳/watchdog基线干净时操作。
  支持模式/同步/分辨率/最高转速/双轮加减速与急停时间/停止策略，以及可选锁轴、驻车、I/O和方向。
  keep项不读取、不写入、不校验，保留现场值；不提供任意OD写入接口。
- drive_pdo_profile=1时，通过临时心跳及Pre-operational确认建立固定app启动PDO基线；保存前恢复心跳。
  drive_pdo_profile=0只核对已有布局。全部期望参数及布局通过后单次写2010:00=1保存，提示人工重启。
- app调用setup.prepare(true,20,speed_resolution,&config.drive)，对全部明确设置的持久参数只读校验。
  原来的临时运行PDO/心跳/watchdog流程保持；这部分退出恢复，持久参数不恢复。

对象宽度与发送授权仍在qualification_gate.c/.h；设备工具复用独立设备白名单，
允许配置SDO及Pre-operational NMT，拒绝RPDO、目标、控制字和其他NMT。
不再单独增加零散speed_resolution写函数，新增参数应同时更新类型化字段表、设备门控及独立peer测试。
保存请求仅表示驱动应答，不证明掉电持久性；保存全部RW参数的影响不局限于文件列出的字段。

软件按手册0x2026:05描述计算raw=rpm×N，但打包目标0x60FF:03的适用倍率/重启生效尚未验证。
run-3095出现疑似十倍速度差；SDO读回一致不是运行倍率已生效的证明。详见TEST_APP.md的两阶段流程。
RuntimeConfig与RuntimeLayoutProof的speed_resolution必须一致，软件目标仍为整数rpm。

后续新增持久参数时扩展设备工具的类型化白名单、宽度/范围/状态校验及专用发送门控，
在app中仅加入对应的只读预检，不把写授权加入control_write_gate.hpp。
临时运行参数仍采用已有初始化/所有权/恢复规则。ROS2参数回调不能直接改设备字典；
持久配置应通过独立维护工具在停用时执行并安排人工重启，不提供任意index写入服务。
测试参考test_device_config_vcan.py（保存与失败路径）、test_app_vcan.py（运行阶段只读）和运行编码单测。

## 7. ROS2节点拆分建议

| 建议模块/节点 | 复用内容 | 对外职责与限制 |
| --- | --- | --- |
| robot_control_node | app编排、ControlCycle、RuntimeSession、Lifecycle、初始化 | 首版唯一硬件控制进程，专属线程拥有控制对象 |
| sbus_node（可后续独立） | Reader、Parser、Source | 生产SBUS快照/健康/原始通道，不发CAN |
| algorithm_command_adapter | 新增消息校验与运动学适配 | 生产external快照，不直接使能或写驱动 |
| arbiter组件/状态节点 | ControlArbiter、ControlCycle | 决策留在执行者内部，对外发布选择原因与状态 |
| canopen组件/诊断节点 | Lifecycle、ObservationStore、RuntimeSession | 内部唯一持有栈，对外发布观测和受限配置结果 |
| can_monitor_node（可选） | 被动总线诊断 | 只观测，不增加第二个最终目标发送入口 |

CANopen若改为经独立can_node的topic收发，需重写epoll、SDO时限、peek/consume、
发送授权、每事件抑制和发送反馈，属于传输适配改造，不是直接节点包装。

独立sbus_node也需要改造应用层：现有ControlLoop::step自行读取Reader并覆盖input.sbus，
直接传入订阅快照不会生效。应保留现有一体入口，另设接收校验后快照的入口，
复用ControlCycle与同一RuntimeSession；不能两个Reader同时读取同一UART。
独立SBUS进程退出时，控制端必须自己超时撤权，不能依赖它发最后一条零命令。

若坚持独立arbiter_node，其输出只能作为候选请求，执行端仍需安全/代际/时限复核；
prepare→submit→complete同步关系不能拆成没有确认的多个topic。
这属于新增分布式控制协议，需要单独设计与验证。

## 8. ROS2消息、线程和时间合同（建议，尚未实现）

建议分别发布SBUS状态、驱动状态、仲裁结果和诊断。命令消息至少保留：

| 数据 | 要求 |
| --- | --- |
| 左右rpm、stop_requested | 明确左右、正方向、单位和范围，拒绝NaN/Inf与转换溢出 |
| sequence/session_generation | 拒绝重复、倒序；进程重启不能复用旧会话/授权 |
| authorization_generation | 由显式重授权流程更新，每条消息递增不等于重新授权 |
| 采样时刻/时钟标识 | 安全期限基于单调时间；转发或重发不能刷新成“现在” |
| valid/coherent/enabled/lost/failsafe | 保留健康信息；断流不能伪装成普通零速度 |

不要直接序列化std::chrono::time_point内存。跨进程需定义共同单调时钟和启动身份，
跨主机不能直接比较steady_clock。只用订阅接收时刻无法识别网络中的旧命令；
必须同时约束源数据年龄/时钟转换或会话期限，无法证明新鲜时拒绝运动。
ROS仿真时间不能直接作为电机安全超时时间。

如果算法输出Twist的v(m/s)、w(rad/s)，需明确实际轮半径r、轮距b和减速比：
左轮角速度=(v-w*b/2)/r，右轮角速度=(v+w*b/2)/r，
乘60/(2π)得到轮rpm；驱动若要求电机rpm还需减速比与方向转换。
这些参数不能由代码猜测，不把Twist数值直接当rpm。

建议命令队列小深度、只留最新、非持久化；可靠性、deadline、lifespan按选定ROS2版本验证。
QoS通知不能替代本地超时，可靠传输也不能证明新鲜。
订阅回调只校验并替换一致快照；服务请求经有界队列给唯一执行者处理。
持久参数仅通过独立配置阶段修改、保存并重启，运行节点只读校验；临时映射仍遵循单一owner生命周期，不让参数回调直接修改运行对象。

executor/回调分组不会自动使现有对象线程安全。
不要从多个回调线程调用Lifecycle、ControlCycle或RuntimeSession，使用固定专属线程。
统一ROS2与TerminationEvent/signalfd的SIGINT/SIGTERM策略；
先请求停车、验证恢复、结束日志，再销毁资源。
RuntimeLog目前在硬件打开前fork，不应直接挪到多线程executor启动之后；
保持初始化顺序，或实现并验证替代收集机制。

## 9. 构建与移植验证

CMake仍是底层构建入口，后续ROS2包可用ament_cmake包装；当前没有完整可colcon构建的ROS2包。
优先链接已有target，避免复制成另一套实现：

- robot_control_input_sbus_protocol、robot_control_input_sbus_source、robot_control_input_sbus_linux。
- robot_control_application_control、robot_control_application_loop。
- robot_control_communication_canopen_runtime及其上游依赖。
- robot_control_platform_linux、robot_control_service_logging。

参见 [application/control/CMakeLists.txt](../../application/control/CMakeLists.txt)、
[communication/canopen/CMakeLists.txt](../../communication/canopen/CMakeLists.txt)、
[app/CMakeLists.txt](../../app/CMakeLists.txt)。
当前运行/初始化是Debug/default-OFF可选模式，不是已提供生产构建。
普通observer模式拒绝发送，不能只链接它就控制电机；保留必要编译定义、门控对象、
链接包装与依赖版本。生产化需独立定义并验证构建模式。

建议按以下顺序交付：

1. 先运行纯Parser/Source/domain测试，保留原app作对照。
2. 单ROS2包装节点复用控制执行者，只发布状态，验证退出与日志。
3. 接external快照和显式切换，验证陈旧/回放/重启/中立重授权。
4. 确有需要再独立SBUS进程，验证消息时间合同与断流。
5. 增加受限字典配置/读回，验证错误恢复，再做目标板零目标验证。
6. 新入口通过有界运动、故障注入、停车及长稳后建立新交付基线。

| 验证对象 | 现有测试参考 |
| --- | --- |
| SBUS协议/健康/档位 | tests/unit/sbus_parser_tests.cpp、tests/unit/sbus_source_tests.cpp |
| UART/断流 | tests/integration/sbus_reader_tests.cpp |
| 仲裁与控制周期 | tests/unit/control_cycle_tests.cpp及domain相关测试 |
| 驱动守卫/运行CAN | tests/unit/canopen_runtime_policy_tests.cpp、tests/unit/canopen_runtime_vcan_tests.cpp |
| SDO门控/初始化/恢复 | tests/unit/canopen_qualification_gate_tests.cpp、tests/unit/control_qualification_vcan_tests.cpp |
| 整体链路 | tests/integration/control_loop_vcan_tests.cpp、tests/integration/test_app_vcan.py |
| 配置/反馈边界 | scripts/test/test_startup_config.py、tests/unit/app_motion_monitor_tests.cpp |

ROS2额外验证发布者死亡、延迟/乱序/重复、旧会话重放、executor阻塞、切源、
CAN重连/boot代际变化、SDO失败及停车恢复。
主机/vcan通过不代表RK3588调度、真实CAN延迟TX或新速度分辨率已验证。

## 10. 阅读顺序与限制

建议main.cpp → linux_loop.cpp → control_cycle.cpp → runtime.cpp，
然后按任务阅读Source或ControlQualification；不要从生成OD推断业务流程。

本文调用链已根据当前源码核对。ROS2发行版未指定，具体API需按团队版本验证；
本次官方在线文档访问失败，因此未声称验证特定ROS2版本API。
历史状态见 [GROUND_HANDOFF_CHECKPOINT_20260920.md](../verification/GROUND_HANDOFF_CHECKPOINT_20260920.md)，
app软件结果见 [TEST_APP_BASELINE.md](../verification/TEST_APP_BASELINE.md)。
