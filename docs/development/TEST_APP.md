# SBUS 整车测试 app

独立入口 robot-control-app，Debug 构建、默认关闭。运动命令只接 SBUS；可选的只读IPC
支持独立ROS2节点查询状态和校验探测值，见[ROS2_IPC.md](ROS2_IPC.md)。
原 robot-control-hil 及其时间/动作边界保持不变。

算法同事的框架/API导航及ROS2移植建议见 [ROS2_PORTING_GUIDE.md](ROS2_PORTING_GUIDE.md)。

## RK3588 本机编译

在已有 CMake、Ninja、C/C++ 编译器及完整子模块的项目根目录执行：

~~~sh
cmake --preset rk3588-native-debug \
  -DROBOT_CONTROL_BUILD_TEST_APP=ON \
  -DROBOT_CONTROL_BUILD_CONTROL_HIL=OFF \
  -DROBOT_CONTROL_BUILD_CANOPEN_RUNTIME=OFF \
  -DROBOT_CONTROL_BUILD_ZLAC_QUALIFICATION=OFF \
  -DROBOT_CONTROL_BUILD_CANOPEN_COMMISSIONING=OFF
cmake --build --preset rk3588-native-debug --target robot-control-app robot-control-device-config --parallel 2
~~~

先做配置检查（不打开 CAN/UART、不创建日志）：

~~~sh
out/build/rk3588-native-debug/app/robot-control-app \
  --config config/rk3588-test.conf \
  --interface can0 --device /dev/ttyACM0 --check-config
~~~

完成新入口的目标回归及现场准备后，手动启动命令为：

~~~sh
out/build/rk3588-native-debug/app/robot-control-app \
  --config config/rk3588-test.conf \
  --interface can0 --device /dev/ttyACM0 --log-dir ./logs
~~~

交接包内的 run-test.sh 已指定同目录配置，可直接传这些参数：

~~~sh
./run-test.sh --interface can0 --device /dev/ttyACM0 --log-dir ./logs
~~~

run_duration_ms 默认0，持续运行至 Ctrl-C/SIGTERM；加 --duration-ms 60000 则在控制循环运行60秒后停止。
初始化/清理各有独立限时，不包含在上述控制时长中。SIGHUP 按 SIGTERM 停止流程处理。
程序不自动重启；CAN失联、急停、反馈异常或日志故障保护退出后，需要排查、重新启动及新授权。
遥控器未开机或SBUS信号丢失时，只撤销行驶授权，通信健康的驱动器保持零速伺服；信号恢复不自动恢复行驶。

## 接口和初始化

- --interface 选择已经存在且 UP 的 SocketCAN 接口，例如 can0/can1。应用不执行 ip link、
  不设置波特率、restart-ms、设备树或网络；这些由系统启动配置负责。可用
  ip -details link show can0 检查系统设置。接口不存在、类型错误或 DOWN 都在驱动通信前报错。
- --device 选择 SBUS 串口，例如 /dev/ttyACM0、/dev/ttyUSB0 或 /dev/serial/by-id/...。
  按现有 Reader 配置100000波特率、8E2及PARMRK；设备驱动不支持时会报错，不降级为其他帧格式。
- 当前配置 profile=rk3588-zlac8015d-v1 固定 node1/controller127、左 low/+1、右 high/-1、
  既有 CH1/CH3/CH6/CH7 标定。它不是任意电机型号/节点配置器。
- 在线读取模式、故障、目标、静止反馈及映射，不要求电机刚上电。只有完整基线吻合才执行
  已限定的易失映射/心跳/看门狗配置；退出归还自己修改的参数。不会写保存参数或自动故障复位。
- app 使用每接口排他锁；它不能阻止其他 CAN 工具直接发帧。不要同时运行 HIL、
  commissioning 或其他驱动写程序。

开始时手柄回中、CH6 释放，等待输入健康后按 CH6 授权。CH6 是按键切换使能，并非松手即禁能；
再次按下关闭使能。重新启用仍需中立和新按键边沿。正常手柄回中输出零，app 可以继续接收下一次操作。
若SBUS失效，撤销运动授权并保持零速；恢复后仍须中立、释放按键和新CH6按下沿。
UART设备读错误、X1、CAN/驱动器故障仍会保护退出，不自动恢复运动。

### 通电待机与CH6行驶授权

本app启用 powered_standby：驱动器伺服使能与遥控行驶授权是两个独立状态。
启动预检通过后，即使遥控器未开机，也执行零目标的6040控制字6→7→15，
依据双轴新鲜状态推进，最长1秒进入Operation Enabled；不能建立保持状态则停止运行。
该流程不产生运动授权，不允许非零目标，不自动故障复位。旧资格测试入口保持原有策略。

| 场景（app正常运行且CAN/驱动器健康） | 行驶权限 | 驱动器输出 |
|---|---|---|
| 遥控器未开机、尚无有效SBUS | 无 | 使能、双轮零速目标 |
| CH6未授权或切换为关闭 | 无 | 双轮减速至零后持续零速伺服，不发送解轴控制字 |
| CH6授权且摇杆中立 | 有 | 使能、双轮零速目标 |
| CH6授权且摇杆有效偏转 | 有 | 受限速度目标 |
| SBUS超时/丢帧/failsafe | 立即撤销 | 双轮零目标，继续零速伺服；重新授权前不恢复行驶 |

日志 enabled 指SBUS行驶授权，armed 指控制器运动授权，新增 servo_enabled 表示
双轴状态字报告Operation Enabled，zero_hold 表示该状态下双轮目标为零。
因此 enabled=0、armed=0、servo_enabled=1、zero_hold=1 是正常通电待机；
这些是协议观测，不能证明实际保持力矩或抱闸已夹紧。

本变更覆盖app预检完成后的通电保持。若要求驱动器上电到app启动前也锁轴，
应单独配置 drive_start_lock=1（0x2007:00），按设备配置阶段保存、重启并验证；
keep不会开启上电锁轴。2026:04的文档只有开关值，未将其推定为Disable Voltage后的锁轴保证。
软件零速伺服不是机械抱闸控制：故障、急停、程序退出及断电仍走原有故障/清理流程，
可能撤销力矩。抱闸接线及制动联动不在本变更内；实体坡道保持能力尚须独立实测。

## 参数

配置使用 key=value。配置内重复/未知键报错；--set key=value 覆盖一次；直接参数与 --set
覆盖同一个键也视为重复。--interface、--device、--log-dir、--duration-ms 分别对应下表字段。

| 字段 | 默认/范围 | 含义 |
| --- | --- | --- |
| interface | can0 | 已由系统配置的 CAN 接口 |
| device | /dev/ttyACM0 | SBUS 串口路径 |
| log_dir | ./logs | 本次运行目录的父目录 |
| max_rpm | 5，当前完整校验允许1..100 | 双轮逻辑目标上限；输出死区1rpm，CH7三档为上限的30%/60%/100% |
| speed_resolution | 1，允许整数1..10；旧配置省略时为1 | 驱动0x2026:05的期望值；设备配置工具写入/保存，app只读校验，不匹配拒绝启动 |
| period_ms | 10，允许1..20 | 控制周期，不保证硬实时 |
| run_duration_ms | 0，允许0..3600000 | 0至收到停止信号，其他值为控制运行时长 |
| log_file_bytes | 67108864，允许65536..1073741824 | 每个日志段最大字节数 |
| log_files | 4，允许1..20 | 每类日志保留的段数，包含当前段 |
| ipc_socket | off；旧配置省略时仍关闭 | 只读IPC绝对路径，小于108字节；父目录须为当前UID所有、权限0700 |

例如 --set log_file_bytes=16777216 --set log_files=8。ipc_socket仅启用只读状态/探测，
不接受旧H1的control_source/command_timeout_ms配置，不授予外部运动权限。

## 测试调参：配置项与源码限制

修改配置后必须重启 app；运行中不热加载。先执行以下命令检查最终生效值，不打开 CAN/UART：

~~~sh
./run-test.sh --interface can0 --device /dev/ttyACM0 --check-config
~~~

日志中的 config.conf 保存该次启动的配置。命令行覆盖优先于文件；排查时同时检查启动命令中的 --set。
配置项范围见上表；接口、日志容量、运行时长和控制周期在允许范围内只需改配置，不需要重新编译。
speed_resolution改变的是设备参数，必须走下述设备配置、保存、人工重启流程；仅修改.conf不足以使其生效。
CAN 波特率、restart-ms 和 link-up 属于系统网络配置，不在 rk3588-test.conf 中设置。

### 目标速度与反馈超速

当前 max_rpm 解析和完整校验均接受1..100，整车app输出死区为1rpm。
旧交接包只接受3..5；新二进制允许25。本文说明工作区行为，不能据此推断板端旧程序已更新。
max_rpm 单位是驱动速度接口的 rpm，不是 m/s；反馈保护使用0.1rpm单位，二者不可直接照抄数值。

| 调整内容 | 源码位置与定位表达式 | 当前值与影响 |
| --- | --- | --- |
| 配置允许的目标速度范围 | application/config/startup.cpp，number(values, "max_rpm", 1, 100) | 接受1..100rpm；最后的100为配置上限 |
| 各级目标限幅 | 同文件的 runtime、arbiter.max_abs_rpm、sbus.maximum_rpm、sbus.gear_rpm；app/main.cpp 初始化 IoContext | 各级总限幅自动使用 max_rpm；CH7三档取30%/60%/100%，无需逐层修改 |
| 反馈绝对超速保护 | app/motion_monitor.hpp，MotionMonitor::observe 中 v < -1000 或 v > 1000 | ±100rpm；超出即记录 hard_speed 并停止 |
| Runtime 配置边界 | domain/drive/runtime.cpp，valid_runtime_config 中 max_abs_rpm <= 1000 | 软件配置边界；25rpm无需修改，1000不是设备或实车已验收速度 |

当前工作区已能设置 max_rpm=25；未来超过100需同时审核配置边界和反馈硬限。
反馈硬限应独立确定，并留出经验证的正常反馈偏差；若确定采用30rpm，比较值应是 -300 和300。
30rpm仅为单位换算示例，不是本项目已经验证的保护阈值。目标上限与反馈硬限分别生效；实际反馈超过硬限仍会保护退出。当前100rpm硬限来自用户工作区修改，不代表已验证100rpm运动。

这些源码改动必须重新编译并替换板端 robot-control-app；只修改配置文件不能改变二进制里的限制。
需同步更新 scripts/test/test_startup_config.py 的允许/拒绝边界、
tests/unit/app_motion_monitor_tests.cpp 的反馈边界，并验证输出限幅、超速停车和实际停止时间。
修改后的速度范围不能直接沿用原低速验收结论。

### 其他会影响测试的固定参数

以下项目当前不是配置键；不要直接追加到 rk3588-test.conf，否则会因未知字段被拒绝。

| 参数 | 当前值 | 查找位置及调整注意 |
| --- | --- | --- |
| 静止确认反馈带 | ±2rpm，即20个0.1rpm | startup.cpp 的 runtime standstill 参数及 motion_monitor.hpp 的稳定保持判断 |
| 待机/未命令轮反馈故障阈值 | ±5rpm，即50个0.1rpm | motion_monitor.hpp；边界包含，仅超出时锁存 uncommanded；不随目标最高速度同比增大 |
| 启动或换向的方向瞬态窗口 | 200ms | motion_monitor.hpp 的 command/observe/phase；时间到后反向超静止带会退出 |
| 零目标后的停止期限与静止保持 | 1s内完成、连续150ms及新反馈帧 | motion_monitor.hpp 的 observe/ok/safe/settled；相关检查应一致，提速后需重新实测停车 |
| 反馈新鲜度 | 200ms | startup.cpp 的 runtime TPDO阈值和 motion_monitor.hpp 的 ok/safe/settled；不可只改一处 |
| SBUS标定、方向和三个档位 | steering=200/1000/1800、throttle=200/993/1800，均不反向；三档为 max_rpm 的30%/60%/100% | startup.cpp；其他源默认参数在 input/sbus/source/source.hpp |
| 输出死区 | 1rpm，绝对值小于1rpm归零，等于1rpm保留 | application/config/startup.cpp 注入 sbus.output_deadband_rpm；不是.conf配置项；Source库默认值及历史HIL仍为3rpm |
| 节点与左右轮映射 | node1/controller127、左low/+1、右high/-1 | 固定硬件 profile；更换设备或映射需开发人员核对完整初始化与收发链路，不能当普通速度参数改 |

测试人员先使用已有配置项；需要越过源码范围时记录目标上限、反馈硬限、修改版本及重新验证结果，
由开发人员交付匹配的新二进制。每次保留实际配置、程序SHA256和本次运行日志，避免把旧程序配新参数误认为已生效。

## CH7低、中、高档（2026-09-21更新）

CH7原始值≤500为低档，501..1499为中档，≥1500为高档。
startup.cpp注入gear_rpm={max(1,max_rpm*3/10),max(1,max_rpm*6/10),max_rpm}，
复用Source既有通道映射、混合和限幅；运行中切CH7在下一批有效输入时生效，不另加斜坡或重新授权。
当前逻辑接口是整数rpm，比例向下取整并至少保留1rpm配置值：

| max_rpm | 低/中/高配置rpm | 经过app的1rpm死区的直行满杆输出rpm |
| --- | --- | --- |
| 1 | 1 / 1 / 1 | 1 / 1 / 1 |
| 2 | 1 / 1 / 2 | 1 / 1 / 2 |
| 3 | 1 / 1 / 3 | 1 / 1 / 3 |
| 5（默认） | 1 / 3 / 5 | 1 / 3 / 5 |
| 25 | 7 / 15 / 25 | 7 / 15 / 25 |
| 100 | 30 / 60 / 100 | 30 / 60 / 100 |

--check-config及运行config.conf会输出gear_rpm与gear_full_throttle_rpm。
gear_rpm由.conf的max_rpm映射；gear_full_throttle_rpm是经过输出死区后的直行满杆目标，
不是实测反馈，也不表示已获运动授权。旧版3rpm死区下默认低档1rpm被过滤，因此显示0；
当前1rpm死区保留1rpm目标，默认显示1,3,5。摇杆中位死区deadband=50保持不变。
逻辑目标仍按整数rpm向零截断，因此低档1rpm在未满杆时仍可能输出0，不提供小数rpm调速。
speed_resolution只负责物理rpm到原始计数的换算，不改变比例或整数取整。
例如max_rpm=25、speed_resolution=10时，三档正向左轮原始目标为70、150、250。
新档位代码需要重编译并替换app，旧二进制不会因文档或配置文件更新而改变三档同速行为。

## 速度分辨率0x2026:05（2026-09-21）

~~~ini
speed_resolution=10
~~~

十进制整数1..10，表示每rpm的命令计数，单位为1/N rpm/count：
1=1rpm，2=0.5rpm，4=0.25rpm，10=0.1rpm。手册中的A在配置中写10，不写0xA或0.1。
依据仓库《ZLAC8015D V4 CANopen通信说明V1.01》印刷页30–31（U16、RW/S、默认1）。

设备参数与运行参数分成两个阶段，两个程序共用同一份rk3588-test.conf，避免期望值分叉。
配置文件用#注释分为“驱动器参数”和“app应用参数”，不引入INI节语法，仍为平坦key=value。
工具处理驱动器参数，app对明确设定的驱动参数只读预检。可选项keep表示不修改也不比较，
不能把keep当成已经验证设备值正确。旧文件缺省的新可选项等价keep，drive_pdo_profile缺省0。
设备工具不打开UART、不运行控制循环。字段清单如下，示例值不是现场实测/验收值：

| 字段 | 对象/类型 | 允许值、示例与含义 |
| --- | --- | --- |
| drive_mode | 6060:00 I8 | 固定3，当前profile仅支持速度模式 |
| drive_sync | 200F:00 U16 | 固定1，双轮同步应用；不是要求发SYNC报文 |
| speed_resolution | 2026:05 U16 | 1..10，默认1；物理倍率尚需确认 |
| drive_quick_stop | 605A:00 I16 | 固定5，当前停车策略不支持任意改成6/7 |
| drive_max_rpm | 2008:00 U16 | 1..1000或keep；示例100；app max_rpm不得超过明确设定值 |
| drive_accel_left_ms/right_ms | 6083:01/02 U32 | 0..32767 ms或keep；示例各500 |
| drive_decel_left_ms/right_ms | 6084:01/02 U32 | 0..32767 ms或keep；示例各500 |
| drive_quick_decel_left_ms/right_ms | 6085:01/02 U32 | 0..32767 ms或keep；示例各10 |
| drive_io_stop | 2026:03 U16 | INPUT1+256×INPUT2，各0锁轴/1阻尼/2解轴；允许0,1,2,256,257,258,512,513,514或keep |
| drive_start_lock | 2007:00 U16 | 0不锁轴/1锁轴或keep，均不使能 |
| drive_parking | 2026:04 U16 | 0关闭/1开启或keep |
| drive_direction | 2026:07 U16 | 0CW/1CCW或keep；须核对软件左右符号，避免重复反转 |
| drive_pdo_profile | 固定配置策略 | 1建立下述固定基线；0只检查已有基线，不改映射 |

注意表中left_ms/right_ms表示两个独立字段，例如drive_accel_left_ms和drive_accel_right_ms。
四项设备模式参数不接受keep；其余数字参数可以keep。所有数字为十进制。
加减速值是时间而非rpm/s；605A=5不使用6085替代正常减速时间。
应用的1s停车期限、±2rpm静止反馈带及±100rpm反馈硬限不会随这些设置改变；
过长减速时间可能不能满足现有停车期限，不能只放宽参数而宣称停车验证通过。
CAN节点/波特率、电机极对数/编码器/电流/PID/制动电阻仍不开放写入，保留厂家/现场整定值。

### 阶段1：设备配置、保存、人工重启

先停止app，确保设备处于禁用/零目标和静止状态，且CAN接口已配置好。示例命令不代表本次已执行实车写入：

~~~sh
# 仅校验文件，无设备访问
./robot-control-device-config --config ./rk3588-test.conf

# 明确执行设备配置与一次EEPROM保存
./robot-control-device-config --config ./rk3588-test.conf --apply
~~~

工具与app使用同一个接口锁，互斥运行。先检查禁用控制字、零目标、无故障和连续150ms的±2rpm静止反馈，
并要求心跳1017=0、watchdog2000=0；先读取全部选定参数，再进行任何写入，缺少对象就拒绝。
参数只在当前值不同于期望时写入，每次均读回；最终逐项再次校验，成功后才单次保存。

当drive_pdo_profile=1，工具临时设置1017原始值500（250ms），观察新心跳，
发送Pre-operational并观察新心跳确认状态，再禁用/清空/写入/启用各PDO映射。
建立的是app启动基线，而不是运行时速度映射：

| PDO | COB-ID | 映射/数量 | 类型与事件定时器原始值 |
| --- | --- | --- | --- |
| 1400/1600 | 201h | 6040:00/16位 + 6060:00/8位，2项 | 类型255，定时器1000 |
| 1800/1A00 | 181h | 6041:00/32位 + 606C:03/32位，2项 | 类型255，定时器100 |
| 1801/1A01 | 281h | 0项，前两映射项清零 | 类型255，定时器0 |

PDO表中的定时器值为原始值，手册单位0.5ms；它们不等于同数值毫秒。
保存前恢复临时心跳到0、再次检查参数/PDO/watchdog基线，不将握手心跳固化。
失败时只尽力恢复自己临时设置的心跳；已写入的持久候选参数/PDO可能仍部分改变，
恢复失败也会报告，不保存、不自动重试、不声称整套恢复。drive_pdo_profile=0要求现有映射已匹配。
最后发送一次0x2010:00=1（U16）保存请求。
这是厂商手册印刷页27及PDO例程的保存命令，不使用0x1010。
注意：该命令保存设备所有RW参数，不能声称只保存分辨率；工具仅主动修改白名单内的参数及明确选择的PDO基线。
即使读回已经一致，显式--apply仍会保存一次，以覆盖“RAM已改、尚未保存”的情况，不能周期调用。

成功输出EEPROM_SAVE_ACKNOWLEDGED RESTART_REQUIRED并提示人工重启驱动器。
工具仅允许Pre-operational NMT，不发送Operational/Reset、RPDO、使能或非零目标，不自动重启或重试保存。
失败输出CONFIGURATION_UNVERIFIED；写入/保存应答丢失时实际结果可能已经改变，不自动重试。
保存成功应答不能证明EEPROM掉电持久性或运行倍率；重启后的校验及实测仍必要。

### 阶段2：app只读校验设备参数

人工重启并完成设备配置验证后，使用同一份配置运行：

~~~sh
./run-test.sh --interface can0 --device /dev/ttyACM0 --log-dir ./logs
~~~

app在任何运行配置写入前读取全部明确设定的驱动参数，并要求与.conf一致；keep项跳过。
分辨率不匹配报告device_configuration_required_speed_resolution_mismatch；其他参数错误带对象/字段上下文。
启动、运行和退出都不会写上述持久参数，也不保存EEPROM；app发送门控拒绝相应写操作。
设备PDO基线与app的现有初始化/清理兼容：app仍临时映射速度RPDO和诊断TPDO，退出恢复基线。
因此drive_pdo_profile管理的是持久启动布局，不代表运行期间完全不改PDO对象。
原有临时PDO/心跳/watchdog配置及停车清理仍按原流程进行，它们不是本次新增的持久参数。

软件当前编码公式仍为RPDO raw=有符号整数rpm×speed_resolution；反馈按0.1rpm解码。
2026-09-21的run-3095在N=10时出现原始目标40、反馈约44rpm及CAN告警/Boot-up，
“保存重启后才生效”是待确认解释，不是已验证结论；0x60FF:03适用倍率也需实车/厂商核实。
不能仅凭SDO读回或此次流程拆分就声称修复速度异常。app不能从相同读回值判断是否真的重启过。
当前逻辑目标仍是整数rpm，N=10不自动提供0.1rpm摇杆指令；分辨率也不改变档位、死区或超速硬限。

## 日志与退出结果

启动时终端打印本次绝对 log_dir。每次建立独立、0700权限的 run-PID-随机串 目录：

| 文件 | 内容 |
| --- | --- |
| config.conf | 最终生效配置与固定映射/时间阈值 |
| application.log | spdlog保留原等级/标签格式；初始化读回、启动、主错误和清理结果 |
| diagnostics.log | spdlog约1Hz状态/时序；使能、输入故障变化立即记录 |
| trace.bin | 有序原始CAN、SBUS事件、每周期目标/反馈/代际和停止记录 |
| logger-error.log | 日志进程初始化或写入失败时的错误原因，仅失败时产生 |

运行终端现在同时显示application和diagnostics格式化记录，包含等级、标签、日期时间、收集进程PID/TID及事件。
application中的stream事件包装原始应用输出；超过240字符的行以stream_fragment分段。
最终格式化记录含前缀及换行最多512字节，超长记录带truncated=1；轮转与终端镜像共用该上限。
原始CAN/SBUS高频数据仅写trace.bin，诊断仍约1Hz或状态变化时输出。前缀PID/TID为日志收集者，业务时间使用记录中的mono_ns。
终端为非阻塞镜像，终端堵塞/断开可丢弃镜像并在console_summary记录dropped_records；文件记录保持，终端不是完整证据。
启动目录提示、配置错误和最终result仍可为普通终端文本。文件写入失败或收集进程卡顿仍触发控制保护。

也可使用 tail -F 实时跟踪打印出的目录，例如 tail -F ./logs/run-实际目录/diagnostics.log。
当前段为原名，.1为最近关闭段，编号更大则更旧；超过 log_files 自动覆盖本次运行最旧段。
旧 run-* 目录不自动删除。长期运行的原始记录只保留配置容量范围，不能把轮转日志当作完整 HIL 证据。

控制线程只做固定256字节非阻塞记录，不做磁盘写入/字符串格式化；独立子进程写盘及轮转。
队列满/破裂、消费者退出或心跳超500ms会停止控制；磁盘慢或满不会让控制线程等写盘。
底层内核调度/驱动延迟仍可能影响实测时间，不声明硬实时。退出最多等日志子进程2秒，异常结果不报完整。

终端 result=0 为到时正常退出；130/143为完成清理的 Ctrl-C/SIGTERM退出；1为运行/清理/日志失败；
2为参数错误。logs_complete=1 只证明该次收集器正常收尾，不代表原始日志从未轮转或物理验收通过。
app_primary 和 app_cleanup 分开记录，不能忽略清理失败。

反馈保护保留±2rpm静止带；当前工作区硬限为±100rpm；启动前静止严格校验。新 app 对选中轴启动前200ms
及有界停车记录短暂反馈波动，200ms后反向超静止带保护退出。零目标后须在1秒内获得至少150ms
连续静止带和新帧。首次达到150ms不提前结束原有1秒停车观察窗口；窗口内再次超带会重置稳定保持，
但不延长停车期限。达到1秒时仍未稳定则退出；窗口结束后或从未命令运动的轴超±5rpm保护退出。
待机反馈超过±2rpm但未超过±5rpm时不触发 uncommanded，也不计为已静止；重新授权、停车与清理的静止确认仍用±2rpm。
每次原始反馈、阶段、极值和累计波动数写入 trace。
这一新入口的200ms窗口及连续运动需通过新的目标回归，不能引用旧 HIL 结果代替。

## 原始记录格式

对象字典文本日志的 index/sub/raw 使用带0x前缀的十六进制（分别4/2/8位），例如
`hil_read index=0x6041 sub=0x00 raw=0x14601460`。参数 before/requested 及基线不匹配详情同样使用十六进制；
速度、时间和其他运行诊断仍为十进制，trace二进制格式不变。

每条为小端32个有符号64位整数：magic=0x31414352(RCA1)、全局ordinal、kind、steady_ns，后接28字段。
kind0/1=CAN RX peek/TX syscall，2=Reader batch，3/4=SBUS frame/rejected，5=cycle，6=stop，7=end。
CAN字段：id、dlc、syscall_result、errno、原始8字节、monitor_fault、累计波动数、左右phase、左右minimum、左右maximum。
极值从监控启动后的首个反馈初始化；此前字段0表示尚未观测，不代表实测零。
phase0/1/2/3=idle/startup/commanded/stopping；速度极值为归一方向后的0.1rpm，原始字节保持不变。
周期字段见 app/main.cpp record_cycle；所有16个原始SBUS通道保留。RX peek/TX成功不是独立线侧交付证明。
轮转保持每条完整，按最大编号到.1再到当前文件拼接；保留段 ordinal 应连续，首ordinal可以非零。

本次仅软件/vcan/交叉构建验证，未运行新 app 的实车试验。CAN延迟TX残余、精密停车测量、带载、
连续地面运行及最终整机长稳仍单列。原始失败/已消费执行器不会由新入口重新授权。
