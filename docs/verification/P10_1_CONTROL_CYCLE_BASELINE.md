# P10.1 — 单控制周期离线整链

日期：2026-09-16。**软件／离线整链范围验收通过**；不包含整链vcan或真实遥控验收。
开发基点：34789bb；分支 codex/phase9-sbus-development。
范围依据 [整链计划](../plans/PHASE9_SBUS_AND_INTEGRATION.md)。

## 交付与契约

- [ControlCycle](../../application/control/control_cycle.hpp) 是纯值、不可复制的单 owner 编排：SBUS／外部 CommandSample → 现有 ControlArbiter → SafetyManager → P8-R RuntimePolicy → 一个最终零／目标 PDO 字节结果。源生产者不持有运行对象，不创建线程、设备入口或发送回调。
- 调用者逐事件调用 observe，按配置 period 调用 tick；初始周期10ms、切源零保持150ms、CiA402转换期限500ms。测试注入时钟与显式虚拟轴绑定。此节点不实现实际调度器，也不宣称硬实时。
- 有效 SBUS 中立快照可以被选择；外部源缺席不阻止 SBUS 启动。仲裁的选择是意图，实际切源必须先撤权、零保持、取得未消费的源授权，再生成新的系统授权。外部控制期间 SBUS 会话／授权变化也撤销外部权限。
- SBUS和外部源分别记录已消费的会话／授权。故障、反馈失效、输入不一致、源丢失、控制周期超过决策期限、非中立新授权等均清权；健康观察恢复不能复用旧授权。重新授权需要有效中立输入及实测双轴静止，随后按状态确认完成使能。
- 源快照要求 age < timeout；未来／倒退接收时间、较旧会话／序号／授权和同序号内容变更均拒绝。同一未过期且内容相同的快照允许重复读取，不刷新其接收时间。公共旧 is_fresh 辅助函数的兼容边界未改变。
- 第一次系统授权只能产生零 Shutdown。后续使能必须先有更新的零速度反馈及更新状态时间；转换在精确期限处失败。等待新反馈时只允许继续零 Shutdown，不能凭等待时间推进使能。停机锁存；任一轴 fault 都禁止运动；不请求故障复位。
- 源会话、源授权、系统授权、决策序号、反馈代际／epoch分别保留；请求封装保留签发时间。RuntimePolicy 对过期／重放决策仍拒绝；observe 中的瞬时fault→健康不能覆盖撤权。

## 行为回归与历史兼容

新增回归先复现7项旧领域缺口：SBUS中立选择、外部连续授权、外部零命令保有源身份、启动非中立、单轴fault、非中立恢复及shutdown锁存。修复后原行为锁继续验证手动优先、限幅、外部零保持与旧授权拒绝。

P2历史记录保持历史状态；本节点明确收紧新授权的中立条件，允许健康中立 SBUS 作为有效零意图，并允许当前外部授权服务连续新快照。原“立即手动选择”不等于绕过新编排的切源零保持。两个新增整链回归分别先复现了慢零反馈过早撤权和外部控制期间SBUS代际变化漏撤权，修复后通过。

## 验证

| 项目 | 结果 |
| --- | --- |
| 最窄领域／运行策略／control_cycle_contract | PASS；含真实 Source 快照、假时钟状态转换、切源、失联、代际与决策重放、并发快照 |
| Host Debug，runtime开启 | 36/36，无skip；最终局部修改另跑整链测试 |
| Host Release，runtime关闭 | 35/35，无skip；最终局部修改另跑整链测试 |
| Clang ASan/UBSan，runtime开启 | 36/36，无skip；本机detect_leaks=0，不宣称LSan |
| 原P6回归，runtime关闭 | 75/75，无skip |
| 范围内静态与CI路由 | clang-tidy、格式、selector回归、actionlint通过；远端CI另行核实 |
| 锁定容器／真实sysroot aarch64与ELF | 最终快照74步PASS；ABI／依赖／符号版本／无RPATH通过 |
| 目标smoke／整链vcan／实机HIL | 本节点不执行；分别留给P10.2／P10.3的适用门槛 |

静态检查采用 clang-analyzer、bugprone、performance、portability；保留项目 pragma once 和显式32位诊断位集。旧 domain_tests 中本次未修改的P6解码断言不在行范围内；其既有optional告警记录于初次日志。新增测试fixture的可互换数值参数有局部解释。并发测试验证原有Source互斥快照完整性，不声称TSan或任意多owner调用安全。

复用已有host构建目录；最窄复现：

~~~sh
cmake -S . -B out/build/p8r-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON -DROBOT_CONTROL_BUILD_CANOPEN_RUNTIME=ON
cmake --build out/build/p8r-debug --parallel 2
ctest --test-dir out/build/p8r-debug --output-on-failure \
  --tests-regex '^(control_cycle_contract|domain_behavior_lock|canopen_runtime_policy)$'
python3 scripts/test/test_ci_scope.py
actionlint .github/workflows/ci.yml
~~~

完整构建身份、测试XML和交叉复现见 [证据](evidence/p10_1_control_cycle_20260916/README.md)。交叉构建使用开发基点上的dirty快照，不冒充clean Release；最终编译文件校验表与快照逐项核对。初次Docker socket沙箱拒绝记录保留，授权后使用原锁定镜像，未重建工具链。

## 边界与后续

ControlCycle的输出是离线计算结果，不证明SDO布局读回或实际发送成功，不能直接重放到总线。P10.2需把单owner编排接入Lifecycle／RuntimeSession，统一运行状态与发送失败处理，保留原session token、布局代际门及逐事件抑制；不能把离线guard与实际发送guard当成两套独立运动权限。

CANopen写入能力继续Debug-only/default-OFF；纯策略库可在Release测试。未增加生产daemon、ROS2、SDO/NMT配置、UART访问或物理控制。P10.3还需真实布局、设备与新硬件授权。P6保持OPEN，内核delayed-TX和物理停车保证不由本节点解决。
