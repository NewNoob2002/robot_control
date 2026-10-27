# 运动 v2 外部授权实施切片 — 2026-10-27

本文件把[外部授权合同](ROS2_EXTERNAL_AUTHORITY_CONTRACT.md)第3步拆成可独立审查的切片。
它是计划，不授权实际设备操作、持久参数写入、部署或运动。

## 现状与缺口

`ControlArbiter`/`ControlCycle` 已有 external 来源、零保持、新授权代际、SBUS接管撤权和
CiA402过渡；缺少合同要求的挑战/授权签发方，以及把候选安全写入 `CycleInput.external` 的入口。
RCI1 诊断 socket 保持只读，不承担运动。

## 关键设计约束

arbiter 在 external 可用、双零持续 `handover_zero_dwell` 且授权代际高于下限时会选中 external，
ControlCycle 随后自动执行 CiA402 使能。因此**未授权时不得向 arbiter 提供任何 external 样本**；
否则一个"观察用"零样本就会在没有显式授权事件的情况下使能驱动。

- REVOKED/资格阶段：authority 输出空 external 样本，SBUS 零输出保持所有权。
- 中立资格由 authority 自行追踪：SBUS 结构有效、新鲜且为零；producer 连续提交经挑战验证的中立候选；
  驱动 healthy 且在现有 standstill 带内；连续满足 `handover_zero_dwell`。
- 只有资格达标**之后**的显式授权请求签发新代际；授权请求本身不计入中立资格，持续发送授权请求
  不会重复签发。签发后 arbiter/cycle 原有零保持照常执行，期间任一条件破坏即撤权。
- external 需要 SBUS 在线且中立，这是既有 arbiter 策略，人工接管始终可用。

## 3a — 纯逻辑 ExternalAuthority（本轮）

位置：`application/control/external_authority.{hpp,cpp}`，无 I/O、无线程，由 control owner 持有。
随机身份（app_instance_id、challenge_id）由调用方注入，3b 再用 `getrandom` 提供。

- 仅允许一个启动配置的 producer；多 producer 不在本切片范围。
- `connect/disconnect` 递增 connection_generation（溢出永久抑制）并撤权。
- 每个连接至多一个在途挑战，绑定 app_instance_id、connection/authorization/drive generation
  （drive generation = `RuntimeState.epoch`）和板端签发时刻 t0；deadline 只在板端。
- 回复进入校验即消耗挑战和序号，拒绝不能修补重放；序号单调递增、不回绕。
- 候选以 t0 作为 `CommandSample.captured_at`，转发不刷新年龄；rpm 超限拒绝而非钳位。
- 撤权事件：断连/重连、挑战或候选超时、drive epoch 变化、驱动不健康、SBUS 失效或非零、
  `stop_requested`、shutdown。撤权后回到 REVOKED，需要重新中立资格和新的授权事件。

### 回归矩阵（先写测试，经真实 authority → ControlCycle → RuntimePolicy 离线链路）

| 合同用例 | 期望 |
| --- | --- |
| 候选年龄=timeout、未来、时钟倒退、无挑战 | 拒绝，输出零；重复转发不刷新年龄 |
| 挑战恰好到期、已消费挑战、旧序号、序号上限、篡改实例/连接/授权/drive代际 | 拒绝，无代际刷新 |
| 新挑战附旧缓存回复、旧授权代际重新包装 | 拒绝 |
| 重连、app重启（新实例）、drive epoch变化、SBUS接管后返回 | 旧候选和代际失效，恢复后仍 REVOKED |
| 非中立授权、dwell前授权、持续授权请求、资格期间断连 | 不签发或清空资格 |
| 正常 SBUS→external 交接、SBUS 抢占、producer 停止回复 | 零保持后新代际；停止回复后到期撤权 |
| 有效候选同周期遇反馈丢失、急停、SIGTERM | 抑制优先，零输出 |

验收：focused 单元测试 → host Debug 全量 CTest → sanitizer → clang-tidy → RK3588 交叉构建。

3a 状态（2026-10-27，主机 GCC 16.2.1 / clang 23.1.4 / CMake 4.3.1）：`external_authority_contract`
通过；`host-test` 全量 36/36（`-Werror`）；ASan/UBSan 下 external_authority、control_cycle、
domain_behavior_lock 3/3；改动文件 clang-tidy 23.1.4 零告警（项目验证版本为 22.1.8）。
5 个变异体（去掉 dwell 门限、忽略 owner 已消耗代际、接收时刷新年龄、去掉序号下限、忽略驱动重启）
均被测试捕获。为让 authority 识别已被 owner 消耗的代际，`ControlCycle` 新增只读
`authority_status()`，未改变任何授权语义。RK3588 交叉构建尚未运行。
3a 不接入 app、IPC 或 ROS；`CycleInput.external` 在可执行程序中仍未被写入。

## 3b — 独立运动 IPC 端点（3a 审查后）

独立 `RCM2` SEQPACKET socket，默认关闭；`SO_PEERCRED` 核验启动配置的 uid/gid；owner 线程有界 poll，
只交付最新候选；结果分别报告 validated/owner_selected/transport_submitted/feedback_observed。
Python fixture 走真实 socket 做 managed vcan 闭环。RCI1 保持只读不变。

## 3c — ROS2 桥接（需单独确认信任边界）

无鉴权的跨机 DDS 默认禁用外部运动。开始前需决定：仅板端本机 ROS 节点作为 producer，
或引入 SROS2/DDS Security。
