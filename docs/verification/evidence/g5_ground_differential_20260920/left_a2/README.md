Single user-authorized ground left differential retry A2 after independently accepted disabled-only baseline restoration. Same51f94c1f/CLI20/right-sign-1,<=5rpm/<3s. New single-use markers; fresh START. No pushing/touching wheels throughout trial; preserve original failures. No automatic retry.

## 2026-09-20 地面左差速 A2 — 按已批准规则验收通过

51f94c1f/CLI20/right-sign -1，先直行后左差速共1779.997ms，13个独立有效左慢右快反馈样本。后续CH1=679/CH3=1800产生(0,5)单轮候选，被提交前拒绝；不是超时或无差速证据。保留原始exit1/primary_ok0。

零目标后412.482ms出现唯一右raw+2.2rpm/逻辑−2.2rpm样本（同帧左−0.4rpm），全程未超过±7.5rpm；该帧位于有界停止且早于稳定段，712.426ms完成双轴连续近零及新帧确认。按用户已批准NORMAL_START_STOP_FEEDBACK_TRANSIENT规则接受，feedback_bad1/feedback_hard_bad1及所有raw保持不变。没有在线保护修改。独立审计分别使用应用与线侧时钟，并验证超速和重复异常篡改被拒绝。

1274帧双捕获一致，36次易失写入与原始基线完整恢复，CAN计数不变，无重启。现场目测前进约20cm、左转弯位移1–2cm、几乎无余动，无X1介入/接触/越1m边界；OFF确认双轮前进停止正常、无异响、驱动断电。保留现场原话，不将转弯位移解释为精确曲率或停车距离。

本次有界无载地面左差速功能通过。原A1手推失败及清理失败保留，独立禁能基线恢复另有通过记录。A2一次性执行器消耗完毕，无自动重试；右差速及剩余G5项目仍待相应证据。复核脚本audit-revised.py，结果revised-acceptance.json。
