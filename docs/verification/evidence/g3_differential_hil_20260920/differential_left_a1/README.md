# Left differential A1 — INCOMPLETE (2026-09-20)

Artifact 51f94c1f, original 3s/5rpm bounds. Motion lasted 609.998ms with six independently verified differential feedback samples. Final healthy SBUS input CH1=704/CH3=1773 mapped to logical (0,5)rpm due to the unchanged 3rpm output deadband. This single-wheel candidate violated the positive dual-wheel envelope and was rejected before submission. Original exit1/primary_ok0 preserved; no timeout or feedback failure. Stable stop verified in355.479ms;1209 matching capture frames,36 volatile writes/full baseline restoration and unchanged CAN counters independently verified. Operator OFF confirms both forward/normal stop/no sound/drive OFF. No retry performed; both runners consumed. See interruption-analysis.json for raw input correlation.


## 2026-09-20 用户批准的差速功能验收

手柄约20%仅为操作建议，不作为验收门槛。已在原3秒/5rpm边界内发送正确正向不等速目标，并有至少两个新的同向且大小关系正确的双轮反馈，随后安全停止、无重启、完整恢复及正常现场反馈，可接受差速功能；后续越界候选被拒绝不抹去此前有效证据。在线保护及原始失败/退出码不变，无新运行授权。
左差速A1按此规则复核PASS：6个有效差速样本，运动609.998ms，稳定停止355.479ms，1209帧双采集一致，36次易失写入完整恢复，CAN计数不变，现场OFF确认。A2没有非零RPDO，仍未完成，不能用于运动验收。右差速仍待验证。
