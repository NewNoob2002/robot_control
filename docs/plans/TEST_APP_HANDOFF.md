# 整车测试 app 交接 — 当前检查点 2026-10-10

开发起点：0f07dbc，标签 checkpoint/ground-functions-20260920。
此前地面功能验收保留在 [GROUND_HANDOFF_CHECKPOINT](../verification/GROUND_HANDOFF_CHECKPOINT_20260920.md)。
当前使用、启动命令和配置以 [TEST_APP.md](../development/TEST_APP.md) 为准；
软件证据与历史迭代见 [TEST_APP_BASELINE.md](../verification/TEST_APP_BASELINE.md)。

| 阶段 | 当前交付 | 状态 |
| --- | --- | --- |
| H1 启动契约 | 配置文件、严格校验、CAN/UART/日志路径/时间/参数覆盖 | 已实现；新增可选只读ipc_socket，默认off |
| H2 IPC | 独立ROS2进程与app的Unix SEQPACKET状态/探测 | 2026-09-21恢复第一阶段；只读通信已实现，外部运动命令仍未接入；见[IPC合同](../development/ROS2_IPC.md) |
| H3 整车入口 | 独立Debug/default-OFF app、SBUS三档差速、零目标在线初始化、单一控制者、通电零速待机与退出恢复 | 软件/vcan验证通过 |
| H4 配置与日志 | 独立设备配置工具、轮转日志子进程、原始trace、spdlog终端镜像、CMake安装 | 日志边界/故障回归与当前managed vcan通过 |
| H5 目标回归 | 当前app配置/纯合同/IPC、私有与原生Humble DDS及板端→host TCP/限定显式UDP状态 | 设备无访问部分、TCP网络中断恢复、授权规则后UDP状态/过期/新实例恢复通过；host RViz smoke通过；真实CAN/UART及实车回归待现场条件，见[实板基线](../verification/IPC_BOARD_BASELINE.md) |

## 当前契约

- CH7按max_rpm的30%/60%/100%取整，最低1rpm；原始通道死区50，输出死区1rpm。
- app只读比对持久参数。独立设备工具支持15个类型化参数及可选固定PDO基线，
  默认只校验配置；显式--apply才写入/读回并请求一次0x2010保存，随后人工重启。
  keep保留现场值，EEPROM应答不证明掉电持久性或速度比例已实测正确。
- 健康驱动器在无遥控/CH6关闭时保持零速伺服，恢复运动须中立和新授权。
  这不等同于断电抱闸或经过验证的坡道保持能力。
- 待机/未命令轮超±5rpm锁存故障；静止确认、重新授权、停车及清理仍为±2rpm，
  停车期限1秒、连续稳定150ms及新帧。方向、硬限和反馈新鲜度检查保留。
- 对象字典日志index/sub/raw及参数原始值用十六进制；速度/时间仍为十进制。
- app-idle5-hex-20260921是历史交付包，不包含后续IPC/spdlog改动。当前源码验证为
  Debug与sanitizer各42/42、四项managed vcan均执行通过；Humble DDS已接入CI并在本地
  执行锁定镜像等价命令，远端Actions未运行本次修改。113个C++输入、最终交叉ELF检查、
  板端→host诊断TCP及500ms接收过期/隔离容器网络中断恢复通过。
  当前产物、原始失败及未验证边界见[补齐证据](../verification/evidence/ipc_link_20261009/README.md)。
- 用户随后授权host wlan0/board161→host198/UDP54710–54711限定规则；已实际新增并保留。
  固定显式UDP两实例388条状态、500ms过期及新实例恢复通过；相邻54712和默认DDS预期阻断。
  UFW/default deny/既有规则保留；需显式选择所存profiles，不扩展到任意ROS参与者。
  规则及回退见[UDP复测](../verification/evidence/ipc_udp_allow_20261010/README.md)。

## 当前复审与后续交接

阶段复审、验证源身份和原失败记录见[交付复审](../verification/evidence/ipc_stage_delivery_20261010/README.md)。
当前只读诊断功能可交付；用户手动fixture/topic通过为人工报告，真实反馈/运动仍未验收。
后续先做最小ROS包/launch与诊断过期显示，再定义状态年龄/运动授权合同，复用external入口
完成离线/vcan，最后另行准备真实零目标和有界运动，见[实施计划](ROS2_IPC_NEXT_STEPS.md)。

## 剩余边界

速度比例实测、机械停止时间/距离、实际保持力矩、抱闸、地面故障、带载与最终整机长稳仍待验证。
本轮仅在独立用户目录部署并验证设备无访问软件；没有真实驱动/运动HIL或EEPROM操作。
旧失败与已消费执行器保持原状。
CI纳入app及设备配置工具的Debug/sanitizer vcan与门控测试；隔离vcan不证明物理行为。
ROS2边界见 [ROS2_PORTING_GUIDE](../development/ROS2_PORTING_GUIDE.md)。

H1历史基线保留在 [TEST_APP_H1_BASELINE](../verification/TEST_APP_H1_BASELINE.md)。
旧版分辨率初始化/恢复已由设备配置与app只读预检两阶段流程取代。
