# P10.2 — PTY / vcan 完整控制闭环

日期：2026-09-16。**本地软件/vcan 验收通过**；远端提交与 CI 单独核实。
开发基点：476a000a928b7d19d26bd98b19624309edd757e0；分支 codex/phase9-sbus-development。
范围依据 [整链计划](../plans/PHASE9_SBUS_AND_INTEGRATION.md)，原始结果见 [证据](evidence/p10_2_control_loop_20260916/README.md)。

## 实现与所有权

- [ControlLoop](../../application/control/linux_loop.hpp) 借用已打开的 Lifecycle、Reader、Source，拥有一个 ControlCycle 和一个 RuntimeSession。全部在同一 owner 调用，无新增线程、设备入口或生产 daemon。运行库继续 Debug-only/default-OFF。
- 每周期先处理 CAN 事件，再读取一个有界 UART 批次并更新 Source；ControlCycle.prepare → RuntimeSession.submit → ControlCycle.complete。RuntimeSession 是唯一运行 guard、逐事件撤权和 RPDO 出口；P10.1 的内部 RuntimePolicy 被移出，避免离线/在线两套授权状态。纯策略测试通过测试适配器保留原覆盖。
- 请求含原始时间、反馈 epoch、授权和决策代际；complete 使用实际提交状态，拒绝错序/重复完成。CAN 瞬时 fault→healthy 不覆盖已经发生的撤权。实际发送失败保留主错误与清理错误，结束循环且不重试。
- 默认周期10ms，按绝对时间调度，错过周期不补发突发命令；记录最大迟到、执行耗时和错过周期数。创建时验证周期、Source/Reader及布局证明，不发送 CAN。
- 继续要求调用者提供当前代际的真实布局读回证明；本节点仅用明确标识的虚拟布局。重建循环会停止旧 Source，Reader 必须显式 reopen 取得新 session，再经过中立、释放和新使能边沿。链路恢复不自动恢复运动。
- stop 显式撤销 Source 并尝试一次安全 RPDO，保留清理失败及超时；默认报告期限100ms，允许配置正值至1s。SIGTERM/事件错误的耗时包含 CAN 等待和 runtime 停止。重复 stop 幂等；析构不隐式发送，调用方必须检查显式 stop 的结果。

## 闭环发现并修复的问题

1. 双轴 quick_stop_active 经新中立授权后，SafetyManager 原来没有恢复到零 Shutdown 的路径。领域回归先复现，随后增加与 switch_on_disabled 相同的零 Shutdown 处理。没有自动故障复位。
2. Lifecycle.run_until 原来先判断 deadline；控制周期已迟到时，排队 SIGTERM 会被延至下一调用，允许先发非零 RPDO。新增闭环用例先复现非零输出，修复为入口先非阻塞消费终止信号。共享生命周期调用点已检查，完整 P6 回归通过。

## 虚拟对端与注入

[整链测试](../../tests/integration/control_loop_vcan_tests.cpp) 仅在项目脚本创建的隔离网络命名空间运行。PTY 注入25字节 SBUS，真实 Reader/Source 产生快照；独立 CAN socket 对端不使用生产编解码器，以 RPDO 字节推进 CiA402 状态并回送心跳、TPDO1速度/状态及TPDO2模式/故障。

- 每个发送帧必须是 0x201 / DLC6；安全动作目标必须为零；顺序为 Shutdown6 → SwitchOn7 → Enable15。禁止意外 SDO/NMT。
- 合成油门/转向产生实际左75、右25rpm字节，连续100个运动周期有反馈闭环。外部源中立切换后连续 -20/-20rpm，旧 SBUS 授权不能夺回运动。
- SBUS flags12、UART静默/断开、急停、单半轴瞬时故障、分别缺少 HB/TPDO1/TPDO2、70ms决策间隔均撤权；健康恢复后必须重新授权。
- boot-up 使布局失效并报告 ESTALE，连零帧也不能向未知布局发送；显式 owner/Reader 重启后仍需新授权。隔离 vcan link-down/up、注入 bus-off、过期实际请求及旧 session 均验证拒绝运动。
- RPDO EIO/短发送、stop EIO 保留失败且不重复写；1ns故意不可满足的报告期限验证 ETIMEDOUT 分支，安全零帧仍被检查。运动 SIGTERM、迟到后排队 SIGTERM 均检查最终零/无后续非零、Source无效及幂等停机。

## 验证结果

| 检查 | 结果 |
| --- | --- |
| 最窄4项：control cycle、loop、runtime policy、domain | 4/4 PASS |
| Host Debug / runtime ON | 37/37 PASS，无skip |
| Host Release / runtime OFF | 35/35 PASS，无skip |
| Clang ASan/UBSan / runtime ON | 最终独立全量37/37 PASS，无skip；本地 detect_leaks=0，不宣称LSan |
| 原P6 / qualification ON、runtime OFF | 75/75 PASS，无skip，86s |
| 范围静态与CI | clang-tidy、格式、selector、actionlint通过；runtime CI新增强制整链vcan，SBUS/UART源码修改也触发runtime套件 |
| 锁定容器 / 真实sysroot aarch64 | 79步构建PASS；新loop测试ELF架构、解释器、依赖/版本、无RPATH及链接符号检查PASS |
| 目标smoke / 真实HIL | 未执行，留给另行授权的P10.3 |

Debug短循环实测：112总周期（含启动）、最大 tick→对端10938µs、最大迟到1048µs、最大周期执行143µs、missed0。最终sanitizer对应10721/887/336µs、missed0。tick→对端含等待周期和测试对端drain，是保守软件观测，不是精确硬件 command-to-RPDO 时间，更不是硬实时承诺。SIGTERM用例断言完成及内部报告均小于100ms；发送成功仅代表内核接收，不证明驱动器停车确认。

首轮sanitizer为36/37：既有 sbus_observer_cli 在 select 等待下一帧日志处超时，无sanitizer诊断。单独该项重跑及无并行构建的完整重跑均通过。未改动该脚本，确切超时根因未确定；原失败记录保留，不把首轮改写为PASS。初次link-down断言错误与迟到SIGTERM实际缺陷也分别保留，详见证据。

## 边界

PTY明确使用100000波特8N2，不能验证物理8E2、反相、电压或线缆。虚拟布局、速度/状态反馈与bus-off注入不是真实驱动器认证；未增加SDO/NMT配置或真实布局读回入口。无目标部署、硬件访问或运动操作。本节点不解决rockchip_canfd延迟TX，P6长稳与内核问题仍OPEN；P10.3须新artifact、明确设备/边界和独立授权。历史授权均不得复用。
