# P9.0 — SBUS 契约离线验收

日期：2026-09-15。状态：**PASS — 离线契约；R8FM／已反相／旧通道映射获操作员确认，未做实机验收。**

## 交付与验收范围

- [P9 计划](../plans/PHASE9_SBUS_AND_INTEGRATION.md)细化 P9.0–P9.3 的交付、
  测试、依赖和每节点提交／push 规则，后续仍按 P9 → P8-R → P10 推进。
- [输入契约](../architecture/SBUS_INPUT_CONTRACT.md) C1–C7 覆盖协议、UART、
  时间与代际、映射、恢复、快照、16 组预期向量及现场阻塞项。
- README 增加计划入口；现有 CI push 分支列表增加
  `codex/phase9-sbus-development`，工作步骤不变。
- 无运行时代码变更，无目标连接、设备配置、CAN 发送或运动。

| P9.0 门槛 | 结果 |
| --- | --- |
| 原始证据可追溯 | PASS：旧工程提交与下列文件摘要固定；三个 SBUS 文件与旧基线无差异 |
| 协议与健康边界明确 | PASS：25-byte 固定 profile、flags、有序事件、100 ms 等于边界失效 |
| 映射和授权没有混用 | PASS：兼容值与实机配置分开；输入 rearm 不代替系统授权 |
| 恢复不自启有预期向量 | PASS：连续健康、中立、release→press、旧代际及输出死区边界均列出 |
| 现场确认与未知项有明确边界 | PASS：操作员确认 R8FM、已反相可直读、旧通道映射；C7 保留 UART 路径／引脚及校准待确认，不视为实机通过 |
| 文档链接／编号／差异与向量核算 | PASS：本节所列离线检查通过 |

## 原始证据身份

只读旧工程 HEAD：`b81fafcc5d914dbf688b5dad5f9a305af6b950bc`。
旧行为基线：`c34042e68aa23fcd789a63a1693a507296935032`。

```text
9feae8d7f4d0d02aceb9f300b76f3a5b7e861ffc23d0f7b9f8fe970197a03847  USER/Config/sbus_config.h
6e883f789495f65e14474daa077adf1b5cf745cfebe5cd4abf7f45b80f7324d0  USER/Input/sbus_parser.c
f3d5ab34bbfbdc5efd86ff9fc798470bcf9bee4c92b83fa369d6d6bb4a4978da  USER/Input/sbus_control_adapter.c
03cef912644da1359f2f03e505f31d5c8bfb173469c507ed76b7be15717257e0  USER/Config/motion_config.h
47ff72924b77f99190ac99126a3b45623dc9e5c1bcaf2652043d857c190ecb17  Platform/stm32g474/Core/Src/usart.c
```

执行旧工程 `git diff c34042e68aa23fcd789a63a1693a507296935032 HEAD --`
加前三个路径，输出为空。工作区状态仅有未跟踪 `.serena/`；未修改旧工程。

## 离线验证

- `git diff --check`：通过。
- Python 标准库检查三个新增 Markdown 的本地链接、C1–C7、V01–V16、
  P9.0–P9.3/P8-R/P10.1–P10.3 编号与旧 Phase 7 链接残留：通过。
- Python 独立算术核算 V03 的 176 个单比特位置，以及 V09–V13 中
  端点归一化、死区、45/15 rpm 混控和非中立却输出零的例子：通过。
  这只验证契约数值；不是尚未实现的解析器或健康层测试通过。
- CI 分支检查：新增分支项恰好一次，除分支列表外原 workflow 字节不变。

本次不运行 host/cross/sanitizer/HIL：没有 C/C++ 或构建行为变更。
push 将触发现有远端 CI，其运行结果以该提交的 Actions 记录为准；
本文不预先声称远端 CI 通过，传输与 SHA 确认在执行后另行报告。

## 下一节点与保留事项

P9.1 可开始固定旧 profile 的离线解析；P9.2 物理门槛及 P9.3 实机配置
须先确认 C7。P9.3 需先测试再实现精确超时与更严格的双输入轴中立判定。
P10 保留纯 SBUS 中立 selection／系统授权衔接，不在 P9.0 修改仲裁。
现行 P6 仍 OPEN，长稳仍 DEFERRED，所有旧 runner 授权保持失效。
