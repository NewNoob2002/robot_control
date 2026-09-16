# P8-R — 双轴 PDO 运行管线与驱动绑定

日期：2026-09-16。**软件／vcan 范围验收通过**；不包含真实遥控运动或生产验收。
开发基点：5c8d13c68c0feba06a7f021c41850760074a86bf；分支 codex/phase9-sbus-development。
计划见 [P8-R](../plans/P8_R_RUNTIME_PIPELINE.md)，证据见 [归档](evidence/p8r_runtime_20260916/README.md)。

## 交付和边界

- [RuntimePolicy](../../domain/drive/runtime.hpp)：纯 C++20，显式注入时间、目标／反馈半字、方向、限幅和超时；默认配置无效。保留原始 status、速度、mode、fault 和接收时间。status/fault 继续用中立 low/high 命名。
- 复用 ZLAC 编解码，将已有 SafetyDecision 和显式系统授权封装为带时间、决策序号、反馈 epoch、transport/boot 的提交；运行会话另有不可复用 session token。
- 新授权必须为零且实测速度在显式静止容差内，先发送 Shutdown＋双轴零，再收到新的零速度反馈才能继续 Switch On／Enable／目标。正常已使能状态下的零命令允许减速，不强制每次摇杆回中重新授权。
- 心跳、TPDO1 状态／速度、TPDO2 模式／故障分别使用严格 age < timeout；未来／倒退时间、旧决策、旧代际、任一轴 fault／状态退出、模式不符、EMCY 或格式错误撤销双轴输出。健康恢复不恢复旧授权。
- [RuntimeSession](../../communication/canopen/runtime.hpp) 借用现有 Lifecycle 的唯一端点，由同一 owner 调用。逐接收事件更新策略，故障随后立即恢复也不能覆盖撤权；无输入时 owner 定时处理过期。发送前再次检查决策期限。
- 只发送一个明确 RPDO，无重试／缓存目标重放。失败或短发送终止本次会话，保留错误，重复 stop 不重试；SIGINT/SIGTERM／owner 退出尝试一次零输出并报告失败。显式 stop 的错误不能被后续调用变成成功。
- boot-up／通信代际改变使布局证据失效；此时不再向未知布局写入，即使是零。需要新的 preflight 和会话。总线失效时不能保证应用零帧送达，仍依赖驱动通信保护及后续物理资格。
- 运行能力由 ROBOT_CONTROL_BUILD_CANOPEN_RUNTIME 显式开启，Debug-only/default-OFF，与 commissioning／P6 qualification 互斥；无运行 CLI、线程、SDO/NMT 下载、映射变更或自动故障复位。原观察器保持禁止发送。

## 布局证据和物理限制

创建会话要求调用方提供当前 transport/boot 的布局读回：正确 COB-ID、type255、每项映射数2、RPDO1=6040:00/16+60ff:03/32、TPDO1=6041:00/32+606c:03/32、TPDO2=6061:00/8+603f:00/32，以及200f=1。
这是受信任启动编排的输入契约，**并非运行库自行执行或证明了实机 SDO preflight**。默认全零证据和不匹配证据均拒绝；P10 必须从真实驱动取得并校验这些记录。

RPDO1／TPDO1 依据 [既有物理修复证据](P6_SYNC_PACKED_PDO_REPAIR.md)；当时试验已恢复原出厂映射，不能假定当前仍可直接使用。TPDO2 的模式＋fault 组合仅完成软件验证，P10.3 需要独立物理验收。测试中的正反向、双轮及100rpm限幅都是虚拟向量，不是运动授权；历史实际资格仍限于已记录的正向单轮工况。

接收时间不证明驱动采样时间或底层队列字节年龄；普通 Linux 调度不提供硬实时。内核 delayed-TX 问题仍未修复。RuntimeSession 及 Lifecycle 必须由同一 owner 使用，Lifecycle 必须更长寿；销毁会话前须显式 stop 并检查结果，析构不隐式发送或保证停车。

## 实际验证

| 检查 | 结果 |
| --- | --- |
| 最窄纯策略／managed vcan | PASS；独立线上字节、零／使能、连续目标、故障→健康、错误帧、boot、旧 session、决策及三类反馈超时、发送失败／短发送、SIGTERM |
| Host Debug，runtime开启 | 35/35，无skip |
| 默认 Host Release，runtime关闭 | 34/34，无skip；包含纯运行策略 |
| Clang ASan/UBSan，runtime开启 | 35/35，无skip；本机 detect_leaks=0，不宣称本机LSan |
| 原 P6 Debug 回归，runtime关闭 | 74/74，无skip；验证共享 Lifecycle 未破坏旧入口 |
| clang-format／范围内 clang-tidy | PASS；runtime及显式runtime编译命令下的Lifecycle，无范围内未处置告警 |
| Debug-only与三种写入模式互斥 | Release+runtime、runtime+qualification、runtime+commissioning 均按预期配置拒绝 |
| 锁定镜像／真实目标sysroot交叉 | 70步 PASS；独立快照、Debug、BUILD_TESTING=ON、runtime=ON |
| aarch64 ELF | 解释器／依赖／GLIBC/GLIBCXX/CXXABI／无RPATH通过；runtime/policy/owner链接，原上游deny gate保留，P6发送gate未链接 |
| CI分流／工作流静态验证 | selector回归和actionlint通过；新增独立PDO runtime分组，选中测试缺失或skip会失败 |
| 目标smoke／实机HIL | 本节点未执行，不使用旧硬件授权；在P10的对应门槛验收 |

独立vcan对端记录10次“submit前→收到RPDO”的最大时间：Debug 53 µs，ASan/UBSan 161 µs，均在预定100ms测试上限内。包含进程调度及接收开销，不是总线或实机时延上限。
初次纯策略测试的19ms用例遗漏上一条授权决策自身的到期条件，已补充一次有效零决策后再独立测试19/20/21ms边界；不是放宽超时。静态检查初次报告 optional访问、测试分支与窄整数转换，已修正；GNU ld wrap函数保留有理由的局部命名例外，初始报告归档。

## 复现

~~~sh
cmake -S . -B out/build/p8r-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON -DROBOT_CONTROL_BUILD_CANOPEN_RUNTIME=ON
cmake --build out/build/p8r-debug --parallel 2
ctest --test-dir out/build/p8r-debug --output-on-failure
python3 scripts/test/test_ci_scope.py
~~~

最窄运行检查使用 --tests-regex '^canopen_runtime_'。vcan通过现有脚本在独立user/network namespace创建并随退出清理；没有物理接口变更。
完整交叉复现命令、镜像ID、sysroot lock和源码快照见归档cross-runtime.py／cross-metadata.json；交叉身份是上述基点上的dirty开发快照，不冒充clean Release。

下一步为 P10.1 仲裁／安全整链，之后 P10.2 vcan 全协议闭环，再于 P10.3 新授权下进行真实遥控器、零目标和架空有界运动测试。P6长稳仍OPEN。
